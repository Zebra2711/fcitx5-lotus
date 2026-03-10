/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-state.h"
#include "lotus-engine.h"
#include "lotus-candidates.h"

#include <cstddef>
#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputpanel.h>
#include <fcitx/menu.h>
#include <fcitx/userinterface.h>

#include <algorithm>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>

#include <thread>
#include "debug.h"

namespace fcitx {
    constexpr int      MAX_SCAN_LENGTH = 15;

    static inline bool isWordBreak(uint32_t ucs4) {
        // Space, tab, newline, carriage return, null, or punctuation/symbols (: ; < = > ? @)
        return ucs4 == ' ' || ucs4 == '\t' || ucs4 == '\n' || ucs4 == '\r' || ucs4 == 0 || (ucs4 >= 58 && ucs4 <= 64);
    }

    LotusState::LotusState(LotusEngine* engine, InputContext* ic) : engine_(engine), ic_(ic) {
        LOG("buf=" + oldPreBuffer_);
        setEngine();
    }

    void LotusState::setEngine() {
        LOG("buf=" + oldPreBuffer_);
        LOG("reset");
        lotusEngine_.reset();
        realMode = modeStringToEnum(engine_->config().mode.value());

        if (engine_->config().inputMethod.value() == "Custom") {
            const auto&        keymaps = *engine_->customKeymap().customKeymap;
            std::vector<char*> charArray;
            charArray.reserve((keymaps.size() * 2) + 1);
            for (const auto& keymap : keymaps) {
                charArray.push_back(const_cast<char*>(keymap.key->data()));   //NOLINT
                charArray.push_back(const_cast<char*>(keymap.value->data())); //NOLINT
            }
            charArray.push_back(nullptr);
            lotusEngine_.reset(NewCustomEngine(charArray.data(), engine_->dictionary(), engine_->macroTable()));
        } else {
            lotusEngine_.reset(NewEngine(engine_->config().inputMethod->data(), engine_->dictionary(), engine_->macroTable()));
        }
        setOption();
        LOG("buf=" + oldPreBuffer_);
    }

    void LotusState::setOption() {
        if (!lotusEngine_)
            return;
        FcitxBambooEngineOption option = {
            .autoNonVnRestore    = *engine_->config().autoNonVnRestore,
            .ddFreeStyle         = *engine_->config().ddFreeStyle,
            .macroEnabled        = *engine_->config().enableMacro,
            .autoCapitalizeMacro = *engine_->config().capitalizeMacro,
            .spellCheckWithDicts = *engine_->config().spellCheck,
            .outputCharset       = engine_->config().outputCharset->data(),
            .modernStyle         = *engine_->config().modernStyle,
            .freeMarking         = *engine_->config().freeMarking,
            .w2u                 = *engine_->config().w2u,
        };

        EngineSetOption(lotusEngine_.handle(), &option);
    }

    void LotusState::send_backspace_uinput(int count) const {
        if (realtextLen - static_cast<unsigned int>(count) > 0)
            realtextLen -= count;
        else
            realtextLen = 0;
        const auto& surr = ic_->surroundingText();
        if (surr.isValid() && ic_->capabilityFlags().test(CapabilityFlag::SurroundingText)) {
            ic_->deleteSurroundingText(-count, static_cast<unsigned int>(count));
        } else {
            for (int i = 0; i < count; ++i) {
                ic_->forwardKey(Key(FcitxKey_BackSpace, KeyState::NoState), false);
            }
        }
    }

    void LotusState::replayBufferToEngine(const std::string& buffer) {
        if (lotusEngine_.handle() == 0U)
            return;

        ResetEngine(lotusEngine_.handle());
        for (uint32_t c : utf8::MakeUTF8CharRange(buffer)) {
            if (c == static_cast<uint32_t>('\b')) {
                EngineProcessKeyEvent(lotusEngine_.handle(), FcitxKey_BackSpace, 0);
            } else {
                EngineProcessKeyEvent(lotusEngine_.handle(), c, 0);
            }
        }
    }

    bool LotusState::isAutofillCertain(const SurroundingText& s) {
        LOG("buf=\"" + oldPreBuffer_ + "\"");
        const unsigned int cursor = s.cursor();
        const unsigned int anchor = s.anchor();
        LOG("cur=" + std::to_string(cursor));
        LOG("anc=" + std::to_string(anchor));
        if (!s.isValid() || oldPreBuffer_.empty()) {
            return false;
        }

        const auto&  text    = s.text();
        const size_t textLen = utf8::length(text);

        // Fix that surrounding text is delay update
        const size_t buffLen       = utf8::length(oldPreBuffer_);
        const size_t pb            = text.find(oldPreBuffer_);
        size_t       rangeStart    = buffLen >= static_cast<size_t>(cursor) ? 0 : static_cast<size_t>(cursor) - buffLen;
        size_t       currSuffixLen = textLen > static_cast<size_t>(cursor) ? textLen - static_cast<size_t>(cursor) : 0;
        if (prevSurrSuffixLen_ != currSuffixLen && cursor < realtextLen.load(std::memory_order_acquire))
            realtextLen.store(cursor, std::memory_order_release);
        prevSurrSuffixLen_    = currSuffixLen;
        const bool sameprefix = pb != std::string::npos && pb >= rangeStart && pb <= static_cast<size_t>(cursor);

        LOG("surr=\"" + text + "\"");
        LOG("sameprefix=" + std::to_string(sameprefix));
        // Detect browser autofill/autocomplete suggestions via selection.
        if (cursor != anchor) {
            unsigned int selectionStart = std::min(anchor, cursor);
            unsigned int selectionEnd   = std::max(anchor, cursor);

            // Only consider it browser autofill if the selection starts at the cursor
            // and extends to the end of the line (common address bar behavior).
            if (selectionStart >= cursor || (selectionStart < cursor && selectionEnd > cursor)) {
                if (!sameprefix)
                    return false;
                // If the selection contains a newline, it's likely a multiline editor (AI ghost text),
                // not a single-line URL/Search bar.
                size_t p = text.find('\n', selectionStart);
                return p == std::string::npos || p >= static_cast<size_t>(selectionEnd);
            }
        }

        if (textLen == static_cast<size_t>(cursor)) {
            realtextLen.store(textLen, std::memory_order_release);
            return false;
        }

        // Heuristic: rapid text growth in a single-line context.
        // Applied only when no newline is present after the cursor to distinguish from AI text in editors.
        if (textLen > static_cast<size_t>(cursor) && cursor == realtextLen.load(std::memory_order_acquire) && text.find('\n', cursor) == std::string::npos && sameprefix)
            return true;

        for (auto v = realtextLen.load(std::memory_order_acquire); v < cursor && !realtextLen.compare_exchange_weak(v, cursor, std::memory_order_acq_rel);)
            ;
        return false;
    }

    void LotusState::handlePreeditMode(KeyEvent& keyEvent, KeySym currentSym) {
        if (EngineProcessKeyEvent(lotusEngine_.handle(), currentSym, keyEvent.rawKey().states()) != 0U)
            keyEvent.filterAndAccept();
        if (auto commit = UniqueCPtr<char>(EnginePullCommit(lotusEngine_.handle()))) {
            if (commit && (*commit.get() != 0)) {
                LOTUS_INFO("Commit: " + std::string(commit.get()));
                ic_->commitString(commit.get());
            }
        }
        ic_->inputPanel().reset();
        UniqueCPtr<char> preedit(EnginePullPreedit(lotusEngine_.handle()));
        if (preedit && (*preedit.get() != 0)) {
            std::string_view view = preedit.get();
            Text             text;
            TextFormatFlags  fmt = TextFormatFlag::NoFlag;
            if (utf8::validate(view))
                text.append(std::string(view), fmt);
            text.setCursor(static_cast<int>(text.textLength()));
            if (ic_->capabilityFlags().test(CapabilityFlag::Preedit))
                ic_->inputPanel().setClientPreedit(text);
            else
                ic_->inputPanel().setPreedit(text);
        }
        ic_->updatePreedit();
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    void LotusState::updateEmojiPageStatus(CommonCandidateList* commonList) {
        if ((commonList == nullptr) || commonList->empty()) {
            return;
        }

        int pageSize = commonList->pageSize();
        if (pageSize <= 0) {
            pageSize = 9;
        }

        int         totalItems  = commonList->totalSize();
        int         currentPage = commonList->currentPage() + 1;
        int         totalPages  = (totalItems + pageSize - 1) / pageSize;

        std::string status = _("Page ") + std::to_string(currentPage) + "/" + std::to_string(totalPages);
        ic_->inputPanel().setAuxDown(Text(status));
    }

    void LotusState::handleEmojiMode(KeyEvent& keyEvent) {
        const KeySym currentSym      = keyEvent.rawKey().sym();
        bool         isCtrlBackspace = isBackspace(currentSym) && ((keyEvent.rawKey().states() & KeyState::Ctrl) != 0U);

        if (keyEvent.key().hasModifier() && !isCtrlBackspace) {
            keyEvent.forward();
            return;
        }

        auto baseList   = ic_->inputPanel().candidateList();
        auto commonList = std::dynamic_pointer_cast<CommonCandidateList>(baseList);
        if (commonList && currentSym >= FcitxKey_1 && currentSym <= FcitxKey_9) {
            int offset      = currentSym - FcitxKey_1;
            int globalIndex = (commonList->currentPage() * commonList->pageSize()) + offset;

            if (globalIndex < commonList->totalSize()) {
                commonList->candidateFromAll(globalIndex).select(ic_);
                keyEvent.filterAndAccept();
                return;
            }
        }

        if (commonList && !commonList->empty()) {
            int  globalCursorIndex = commonList->globalCursorIndex();
            int  totalSize         = commonList->totalSize();
            int  currentPage       = commonList->currentPage();
            int  pageSize          = commonList->pageSize();
            int  localCursorIndex  = globalCursorIndex - (currentPage * pageSize);

            bool handled = false;

            switch (currentSym) {
                case FcitxKey_Tab:
                case FcitxKey_Down: {
                    if (globalCursorIndex == totalSize - 1) {
                        commonList->setGlobalCursorIndex(globalCursorIndex);
                    } else if (localCursorIndex < pageSize - 1) {
                        commonList->setGlobalCursorIndex(globalCursorIndex + 1);
                    } else {
                        commonList->next();
                        int newPage = commonList->currentPage();
                        commonList->setGlobalCursorIndex(newPage * pageSize);
                    }
                    handled = true;
                    break;
                }

                case FcitxKey_ISO_Left_Tab:
                case FcitxKey_Up: {
                    if (globalCursorIndex == 0) {
                        commonList->setGlobalCursorIndex(globalCursorIndex);
                    } else if (localCursorIndex > 0) {
                        commonList->setGlobalCursorIndex(globalCursorIndex - 1);
                    } else {
                        commonList->prev();
                        int newPage  = commonList->currentPage();
                        int newIndex = (newPage * pageSize) + pageSize - 1;
                        commonList->setGlobalCursorIndex(newIndex);
                    }
                    handled = true;
                    break;
                }
                case FcitxKey_Page_Down:
                case FcitxKey_Right: {
                    if (commonList->hasNext()) {
                        commonList->next();
                        int newPage = commonList->currentPage();
                        commonList->setGlobalCursorIndex(newPage * pageSize);
                        handled = true;
                    }
                    break;
                }
                case FcitxKey_Page_Up:
                case FcitxKey_Left: {
                    if (commonList->hasPrev()) {
                        commonList->prev();
                        int newPage = commonList->currentPage();
                        commonList->setGlobalCursorIndex(newPage * pageSize);
                        handled = true;
                    }
                    break;
                }
                default: break;
            }

            if (handled) {
                updateEmojiPageStatus(commonList.get());
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                keyEvent.filterAndAccept();
                return;
            }
        }

        if (isBackspace(currentSym)) {
            if (!emojiBuffer_.empty()) {
                if (isCtrlBackspace) {
                    emojiBuffer_.clear();
                } else {
                    emojiBuffer_.pop_back();
                    while (!emojiBuffer_.empty() && (emojiBuffer_.back() & 0xC0) == 0x80) {
                        emojiBuffer_.pop_back();
                    }
                }
                keyEvent.filterAndAccept();
            } else {
                keyEvent.forward();
            }
            updateEmojiPreedit();
            return;
        }

        switch (currentSym) {
            case FcitxKey_space:
            case FcitxKey_Return: {
                if (commonList && !commonList->empty()) {
                    int globalIdx = commonList->globalCursorIndex();
                    commonList->candidateFromAll(globalIdx).select(ic_);
                    keyEvent.filterAndAccept();
                } else if (currentSym == FcitxKey_Return && !emojiBuffer_.empty()) {
                    ic_->commitString(emojiBuffer_);
                    emojiBuffer_.clear();
                    LOG("reset");
                    updateEmojiPreedit();
                    keyEvent.filterAndAccept();
                } else {
                    keyEvent.forward();
                }
                return;
            }

            case FcitxKey_Escape: {
                emojiBuffer_.clear();
                emojiCandidates_.clear();
                LOG("reset");
                ic_->inputPanel().reset();
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                keyEvent.filterAndAccept();
                return;
            }

            default: break;
        }

        {
            std::string utf8Char = Key::keySymToUTF8(currentSym);
            if (!utf8Char.empty()) {
                emojiBuffer_ += utf8Char;
                keyEvent.filterAndAccept();
                updateEmojiPreedit();
            } else {
                keyEvent.forward();
            }
        }
    }
    void LotusState::updateEmojiPreedit() {
        if (emojiBuffer_.empty()) {
            emojiCandidates_ = engine_->emojiLoader().history();
            if (emojiCandidates_.empty()) {
                ic_->inputPanel().reset();
                ic_->updatePreedit();
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                return;
            }
        } else {
            emojiCandidates_ = engine_->emojiLoader().search(emojiBuffer_);
        }

        if (!emojiBuffer_.empty()) {
            Text preeditText;
            preeditText.append(emojiBuffer_, TextFormatFlag::Underline);
            preeditText.setCursor(static_cast<int>(preeditText.textLength()));
            if (ic_->capabilityFlags().test(CapabilityFlag::Preedit))
                ic_->inputPanel().setClientPreedit(preeditText);
            else
                ic_->inputPanel().setPreedit(preeditText);
        } else {
            ic_->inputPanel().setClientPreedit(Text());
            ic_->inputPanel().setPreedit(Text());
        }

        if (!emojiCandidates_.empty()) {
            auto candidateList = std::make_unique<CommonCandidateList>();
            candidateList->setLayoutHint(CandidateLayoutHint::Vertical);
            candidateList->setPageSize(9);

            for (size_t i = 0; i < emojiCandidates_.size(); ++i) {
                size_t localIndex = (i % 9) + 1;
                Text   displayLabel;
                if (emojiBuffer_.empty()) {
                    displayLabel.append(std::to_string(localIndex) + ": " + emojiCandidates_[i].output, TextFormatFlag::NoFlag);
                } else {
                    displayLabel.append(std::to_string(localIndex) + ": " + emojiCandidates_[i].trigger + " " + emojiCandidates_[i].output, TextFormatFlag::NoFlag);
                }
                candidateList->append(std::make_unique<EmojiCandidateWord>(displayLabel, this, emojiCandidates_[i]));
            }
            candidateList->setGlobalCursorIndex(0);

            ic_->inputPanel().setCandidateList(std::move(candidateList));
            auto currentList = std::dynamic_pointer_cast<CommonCandidateList>(ic_->inputPanel().candidateList());
            updateEmojiPageStatus(currentList.get());
        } else {
            ic_->inputPanel().setCandidateList(nullptr);
        }

        ic_->updatePreedit();
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    bool LotusState::handleUInputKeyPress(KeyEvent& event, KeySym currentSym, int sleepTime) {
        LOG("buf=" + oldPreBuffer_);
        if (!is_deleting_.load()) {
            return false;
        }
        if (isBackspace(currentSym)) {
            current_backspace_count_ += 1;
            LOG("buf=" + oldPreBuffer_);
            if (current_backspace_count_ < expected_backspaces_) {
                return false; // Allow intermediate backspaces to reach the app to clear autofill/old text.
            }
            is_deleting_.store(false);
            replacement_start_ms_.store(0, std::memory_order_release);
            replacement_thread_id_.store(0, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
            // Validate surr cursor pos should match realtextLen after all BS applied
            const auto& surr = ic_->surroundingText();
            if (surr.isValid() && surr.cursor() == realtextLen.load(std::memory_order_acquire)) {
                LOTUS_INFO("Skip retry");
            } else {
                // Retry x3 (2 ms each), khi can (chromium,electron,...)
                for (int retry = 0; retry < 3; ++retry) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    const auto& surr2 = ic_->surroundingText();
                    if (surr2.isValid() && surr2.cursor() == realtextLen.load(std::memory_order_acquire)) {
                        break;
                    }
                }
            }
            ic_->commitString(pending_commit_string_);
            LOTUS_INFO("Commit: " + pending_commit_string_);
            expected_backspaces_     = 0;
            current_backspace_count_ = 0;
            pending_commit_string_   = "";

            event.filterAndAccept(); // Filter out the final trigger backspace.
            // if (std::string(ic_->frontend()) == "dbus" && !ic_->surroundingText().isValid())
            //     replayBufferedKeys(); // Does we need drop this?
            return true;
        }
        return false;
    }

    void LotusState::performReplacement(const std::string& deletedPart, const std::string& addedPart) {
        LOTUS_INFO("Perform replacement: " + deletedPart + " -> " + addedPart); //NOLINT
        LOG("buf=" + oldPreBuffer_);
        const auto& surrounding    = ic_->surroundingText();
        int         autofillOffset = isAutofillCertain(surrounding) ? 1 : 0;
        int         bsCount        = static_cast<int>(utf8::length(deletedPart)) + autofillOffset;
        std::string commitStr      = addedPart;
        if (!commitStr.empty() && commitStr.back() == ' ')
            commitStr.pop_back();
        send_backspace_uinput(bsCount);
        if (!commitStr.empty()) {
            ic_->commitString(commitStr);
            LOG("Commit: " + commitStr);
        }
    }

    void LotusState::checkForwardSpecialKey(KeyEvent& keyEvent, KeySym& currentSym) {
        if (keyEvent.key().isCursorMove() || currentSym == FcitxKey_Tab || currentSym == FcitxKey_KP_Tab || currentSym == FcitxKey_ISO_Left_Tab || currentSym == FcitxKey_Escape ||
            keyEvent.key().hasModifier()) {
            is_deleting_.store(false, std::memory_order_release);
            expected_backspaces_     = 0;
            current_backspace_count_ = 0;
            pending_commit_string_.clear();
            history_.clear();
            ResetEngine(lotusEngine_.handle());
            LOG("buf=" + oldPreBuffer_);
            LOG("clearbuff");
            oldPreBuffer_.clear();
            keyEvent.forward();
            return;
        }

        if (currentSym == FcitxKey_Delete) {
            keyEvent.forward();
            return;
        }

        if (currentSym >= FcitxKey_KP_0 && currentSym <= FcitxKey_KP_9) {
            currentSym = static_cast<KeySym>(FcitxKey_0 + (currentSym - FcitxKey_KP_0));
        }

        switch (currentSym) {
            case FcitxKey_KP_Add: {
                currentSym = FcitxKey_plus;
                break;
            }
            case FcitxKey_KP_Subtract: {
                currentSym = FcitxKey_minus;
                break;
            }
            case FcitxKey_KP_Divide: {
                currentSym = FcitxKey_slash;
                break;
            }
            case FcitxKey_KP_Multiply: {
                currentSym = FcitxKey_asterisk;
                break;
            }
            case FcitxKey_KP_Decimal: {
                currentSym = FcitxKey_period;
                break;
            }
            case FcitxKey_KP_Enter: {
                currentSym = FcitxKey_Return;
                break;
            }
            case FcitxKey_KP_Equal: {
                currentSym = FcitxKey_equal;
                break;
            }
            case FcitxKey_KP_Space: {
                currentSym = FcitxKey_space;
                break;
            }
            default: break;
        }
    }

    void LotusState::handleUinputMode(KeyEvent& keyEvent, KeySym currentSym, bool checkEmptyPreedit) {
        LOG("buf=" + oldPreBuffer_);
        checkForwardSpecialKey(keyEvent, currentSym);

        if (isBackspace(currentSym) || currentSym == FcitxKey_Return) {
            if (isBackspace(currentSym)) {
                history_.push_back('\b');
                replayBufferToEngine(history_);
                UniqueCPtr<char> preeditC(EnginePullPreedit(lotusEngine_.handle()));
                LOG("buf=" + oldPreBuffer_);
                oldPreBuffer_ = (preeditC && (*preeditC.get() != 0)) ? preeditC.get() : "";
                LOG("buf=" + oldPreBuffer_);
            } else {
                history_.clear();
                ResetEngine(lotusEngine_.handle());
                LOG("buf=" + oldPreBuffer_);
                oldPreBuffer_.clear();
            }
            keyEvent.forward();
            return;
        }

        std::string keyUtf8 = Key::keySymToUTF8(currentSym);
        if (keyUtf8.empty()) {
            keyEvent.forward();
            return;
        }

        bool processed = EngineProcessKeyEvent(lotusEngine_.handle(), currentSym, keyEvent.rawKey().states()) != 0U;

        auto commitF = UniqueCPtr<char>(EnginePullCommit(lotusEngine_.handle()));
        if (commitF && (*commitF.get() != 0)) {
            std::string commitStr = commitF.get();
            std::string commonPrefix;
            std::string deletedPart;
            std::string addedPart;
            compareAndSplitStrings(oldPreBuffer_, commitStr, commonPrefix, deletedPart, addedPart);
            LOG("buf=" + oldPreBuffer_);
            LOG("commit=" + commitStr);
            LOG("prefix=" + commonPrefix);
            LOG("del=" + deletedPart);
            LOG("add=" + addedPart);
            if (!deletedPart.empty()) {
                keyEvent.filterAndAccept();
                performReplacement(deletedPart, addedPart);
            } else {
                bool wasAutoCapitalized = (currentSym != keyEvent.rawKey().sym());
                if (!addedPart.empty() && (keyUtf8 != addedPart || wasAutoCapitalized)) {
                    ic_->commitString(addedPart);
                    LOTUS_INFO("Commit: " + addedPart);
                    keyEvent.filterAndAccept();
                } else {
                    keyEvent.forward();
                }
            }

            history_.clear();
            ResetEngine(lotusEngine_.handle());
            LOG("buf=" + oldPreBuffer_);
            oldPreBuffer_.clear();

            return;
        }

        if (!processed) {
            if (checkEmptyPreedit) {
                UniqueCPtr<char> preeditC(EnginePullPreedit(lotusEngine_.handle()));
                if (!preeditC || (*preeditC.get() == 0)) {
                    history_.clear();
                    LOG("buf=" + oldPreBuffer_);
                    oldPreBuffer_.clear();
                    keyEvent.forward();
                }
            }
            return;
        }

        history_ += keyUtf8;
        realtextLen.fetch_add(1, std::memory_order_acq_rel);

        replayBufferToEngine(history_);

        auto commitAfterReplay = UniqueCPtr<char>(EnginePullCommit(lotusEngine_.handle()));
        if (commitAfterReplay && (*commitAfterReplay.get() != 0)) {
            std::string commitStr = commitAfterReplay.get();
            std::string commonPrefix;
            std::string deletedPart;
            std::string addedPart;
            compareAndSplitStrings(oldPreBuffer_, commitStr, commonPrefix, deletedPart, addedPart);

            LOG("buf=" + oldPreBuffer_);
            LOG("commit=" + commitStr);
            LOG("prefix=" + commonPrefix);
            LOG("del=" + deletedPart);
            LOG("add=" + addedPart);

            keyEvent.filterAndAccept();
            if (!deletedPart.empty()) {
                performReplacement(deletedPart, addedPart);
            } else if (!addedPart.empty()) {
                ic_->commitString(addedPart);
                LOTUS_INFO("Commit: " + addedPart);
            }

            history_.clear();
            ResetEngine(lotusEngine_.handle());
            LOG("buf=" + oldPreBuffer_);
            oldPreBuffer_.clear();
            return;
        }

        UniqueCPtr<char> preeditC(EnginePullPreedit(lotusEngine_.handle()));
        std::string      preeditStr = (preeditC && (*preeditC.get() != 0)) ? preeditC.get() : "";

        std::string      commonPrefix;
        std::string      deletedPart;
        std::string      addedPart;
        if (compareAndSplitStrings(oldPreBuffer_, preeditStr, commonPrefix, deletedPart, addedPart) != 0) {
            LOG("buf=" + oldPreBuffer_);
            LOG("preeditStr=" + preeditStr);
            LOG("prefix=" + commonPrefix);
            LOG("del=" + deletedPart);
            LOG("add=" + addedPart);
            if (deletedPart.empty()) {
                bool isCommit           = false;
                bool wasAutoCapitalized = (currentSym != keyEvent.rawKey().sym());
                if (!addedPart.empty()) {
                    oldPreBuffer_ = preeditStr;
                    if (addedPart != keyUtf8 || wasAutoCapitalized) {
                        ic_->commitString(addedPart);
                        LOTUS_INFO("Commit: " + addedPart);
                        keyEvent.filterAndAccept();
                        isCommit = true;
                    }
                }
                if (!isCommit) {
                    keyEvent.forward();
                }
            } else {
                keyEvent.filterAndAccept();
                performReplacement(deletedPart, addedPart);
                history_.clear();
                ResetEngine(lotusEngine_.handle());
                LOG("buf=" + oldPreBuffer_);
                oldPreBuffer_.clear();
                LOG("preedit=" + preeditStr);
            }
        }
    }

    void LotusState::handleSurroundingText(KeyEvent& keyEvent, KeySym currentSym) {
        checkForwardSpecialKey(keyEvent, currentSym);
        auto* ic = keyEvent.inputContext();
        if ((ic == nullptr) || !ic->capabilityFlags().test(CapabilityFlag::SurroundingText)) {
            LOTUS_WARN("Surrounding text not supported");
            keyEvent.forward();
            return;
        }

        const auto& surrounding = ic->surroundingText();
        if (!surrounding.isValid()) {
            LOTUS_WARN("Surrounding text is invalid");
            keyEvent.forward();
            return;
        }

        if (isBackspace(keyEvent.rawKey().sym())) {
            ResetEngine(lotusEngine_.handle());
            keyEvent.forward();
            return;
        }

        if (surrounding.anchor() != surrounding.cursor()) {
            ic->deleteSurroundingText(0, 0);
        }

        const std::string& text   = surrounding.text();
        unsigned int       cursor = surrounding.cursor();

        size_t             textLen = utf8::lengthValidated(text);

        if (textLen == utf8::INVALID_LENGTH || cursor <= 0 || cursor > textLen) {
            processNormalKey(keyEvent, currentSym);
            return;
        }

        {
            auto startIter = utf8::nextNChar(text.begin(), cursor);
            auto endIter   = startIter;

            int  scanCount = 0;
            while (startIter != text.begin() && scanCount < MAX_SCAN_LENGTH) {
                auto prev = startIter;
                if (prev != text.begin()) {
                    --prev;
                    while (prev != text.begin() && ((*prev & 0xC0) == 0x80)) {
                        --prev;
                    }
                }

                uint32_t ucs4 = utf8::getChar(prev, text.end());

                if (isWordBreak(ucs4))
                    break;

                startIter = prev;
                ++scanCount;
            }

            std::string oldWord(startIter, endIter);

            if (oldWord.empty()) {
                processNormalKey(keyEvent, currentSym);
                return;
            }

            EngineRebuildFromText(lotusEngine_.handle(), oldWord.c_str());

            bool processed = EngineProcessKeyEvent(lotusEngine_.handle(), currentSym, keyEvent.rawKey().states()) != 0U;

            if (!processed) {
                keyEvent.forward();
                ResetEngine(lotusEngine_.handle());
                return;
            }

            auto        commitPtr  = UniqueCPtr<char>(EnginePullCommit(lotusEngine_.handle()));
            auto        preeditPtr = UniqueCPtr<char>(EnginePullPreedit(lotusEngine_.handle()));

            std::string newWord;
            if (commitPtr && (*commitPtr.get() != 0))
                newWord += commitPtr.get();
            if (preeditPtr && (*preeditPtr.get() != 0))
                newWord += preeditPtr.get();

            std::string commonPrefix;
            std::string deletedPart;
            std::string addedPart;
            compareAndSplitStrings(oldWord, newWord, commonPrefix, deletedPart, addedPart);
            if (deletedPart.empty() && addedPart == keyEvent.key().toString()) {
                ResetEngine(lotusEngine_.handle());
                keyEvent.forward();
                return;
            }

            if (!deletedPart.empty() || !addedPart.empty()) {
                size_t charsToDelete = utf8::length(deletedPart);

                if (charsToDelete > 0) {
                    ic->deleteSurroundingText(-static_cast<int>(charsToDelete), static_cast<int>(charsToDelete));
                }

                if (!addedPart.empty()) {
                    ic->commitString(addedPart);
                    LOTUS_INFO("Commit: " + addedPart);
                }

                ResetEngine(lotusEngine_.handle());
                keyEvent.filterAndAccept();
                return;
            }

            ResetEngine(lotusEngine_.handle());
            keyEvent.filterAndAccept();
            return;
        }
    }

    void LotusState::processNormalKey(KeyEvent& keyEvent, KeySym currentSym) {
        auto* ic = keyEvent.inputContext();
        ResetEngine(lotusEngine_.handle());
        bool processed = EngineProcessKeyEvent(lotusEngine_.handle(), currentSym, keyEvent.rawKey().states()) != 0U;
        if (processed) {
            auto        commitPtr  = UniqueCPtr<char>(EnginePullCommit(lotusEngine_.handle()));
            auto        preeditPtr = UniqueCPtr<char>(EnginePullPreedit(lotusEngine_.handle()));
            std::string out;
            if (commitPtr && (*commitPtr.get() != 0))
                out += commitPtr.get();
            if (preeditPtr && (*preeditPtr.get() != 0))
                out += preeditPtr.get();

            if (!out.empty()) {
                LOTUS_INFO("Commit: " + out);
                ic->commitString(out);
            }

            ResetEngine(lotusEngine_.handle());
            keyEvent.filterAndAccept();
        } else {
            keyEvent.forward();
        }
    }

    void LotusState::handleDoubleSpaceReplacement() {
        switch (realMode) {
            case LotusMode::SurroundingText: {
                ic_->deleteSurroundingText(-1, 1);
                ic_->commitString(". ");
                LOTUS_INFO("Commit: . ");

                break;
            }
            default: { // Uinput, Smooth, Preedit, etc.
                performReplacement(" ", ". ");
                LOTUS_INFO("Commit: . ");
                break;
            }
        }
        if (*engine_->config().autoCapitalizeAfterPunctuation) {
            isPrevPunctuation_ = true;
            shouldCapitalize_  = true;
        }
    }

    void LotusState::keyEvent(KeyEvent& keyEvent) {
        if (!lotusEngine_ || keyEvent.isRelease())
            return;
        if (needEngineReset.load() && realMode != LotusMode::Off) {
            LOTUS_INFO("Need engine reset");
            LOG("buf=" + oldPreBuffer_);
            oldPreBuffer_.clear();
            history_.clear();
            ResetEngine(lotusEngine_.handle());
            isPrevSpace_       = false;
            shouldCapitalize_  = false;
            isPrevPunctuation_ = false;
            needEngineReset.store(false);
        }

        if (g_mouse_clicked.load(std::memory_order_acquire) && !is_deleting_.load(std::memory_order_acquire)) {
            g_mouse_clicked.store(false, std::memory_order_release);
            clearAllBuffers();
        }

        KeySym currentSym = keyEvent.rawKey().sym();
        if (*engine_->config().autoCapitalizeAfterPunctuation && realMode != LotusMode::Off) {
            if (shouldCapitalize_) {
                if (currentSym >= FcitxKey_a && currentSym <= FcitxKey_z) {
                    auto upperSym = static_cast<KeySym>(currentSym - (FcitxKey_a - FcitxKey_A));
                    currentSym    = upperSym;
                    keyEvent.setKey(Key(upperSym, keyEvent.rawKey().states()));
                    shouldCapitalize_ = false;
                } else if (currentSym != FcitxKey_space) {
                    shouldCapitalize_ = false;
                }
            }

            switch (currentSym) {
                case FcitxKey_period:
                case FcitxKey_exclam:
                case FcitxKey_question: isPrevPunctuation_ = true; break;
                case FcitxKey_Return:
                case FcitxKey_KP_Enter:
                    shouldCapitalize_  = true;
                    isPrevPunctuation_ = false;
                    break;
                case FcitxKey_space:
                    if (isPrevPunctuation_) {
                        shouldCapitalize_  = true;
                        isPrevPunctuation_ = false;
                    }
                    break;
                default:
                    if (currentSym != FcitxKey_space) {
                        isPrevPunctuation_ = false;
                    }
                    break;
            }
        }

        if (*engine_->config().doubleSpaceToPeriod && realMode != LotusMode::Off) {
            if (currentSym == FcitxKey_space) {
                if (isPrevSpace_) {
                    keyEvent.filterAndAccept();
                    handleDoubleSpaceReplacement();
                    isPrevSpace_ = false;
                    return;
                }
                isPrevSpace_ = true;
            } else {
                isPrevSpace_ = false;
            }
        }

        switch (realMode) {
            case LotusMode::Uinput:
            case LotusMode::Smooth: {
                handleUinputMode(keyEvent, currentSym, true);
                break;
            }
            case LotusMode::UinputHC: {
                handleUinputMode(keyEvent, currentSym, false);
                break;
            }
            case LotusMode::SurroundingText: {
                handleSurroundingText(keyEvent, currentSym);
                break;
            }
            case LotusMode::Preedit: {
                handlePreeditMode(keyEvent, currentSym);
                break;
            }
            case LotusMode::Emoji: {
                handleEmojiMode(keyEvent);
                break;
            }
            default: {
                break;
            }
        }
    }

    void LotusState::reset() {
        LOG("buf=" + oldPreBuffer_);
        const auto& surrounding = ic_->surroundingText();
        const auto& text        = surrounding.text();
        size_t      textLen     = utf8::length(text);
        realtextLen.store(textLen, std::memory_order_release);
        if (surrounding.isValid()) {
            prevSurrSuffixLen_ = textLen > static_cast<size_t>(surrounding.cursor()) ? textLen - static_cast<size_t>(surrounding.cursor()) : 0;
        }
        if (is_deleting_.load(std::memory_order_acquire)) {
            return;
        }
        is_deleting_.store(false);

        if (lotusEngine_) {
            isPrevSpace_       = false;
            shouldCapitalize_  = false;
            isPrevPunctuation_ = false;
            if (realMode == LotusMode::Preedit) {
                EngineCommitPreedit(lotusEngine_.handle());
                UniqueCPtr<char> commit(EnginePullCommit(lotusEngine_.handle()));
                if (commit && (*commit.get() != 0)) {
                    ic_->commitString(commit.get());
                    LOTUS_INFO("Commit: " + std::string(commit.get()));
                }
            }
            ResetEngine(lotusEngine_.handle());
        }
        if (std::string(ic_->frontend()) != "dbus")
            clearAllBuffers();

        switch (realMode) {
            case LotusMode::Preedit: {
                ic_->inputPanel().reset();
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                ic_->updatePreedit();
                break;
            }
            case LotusMode::SurroundingText:
            case LotusMode::Uinput:
            case LotusMode::UinputHC:
            case LotusMode::Smooth: {
                ic_->inputPanel().reset();
                break;
            }
            case LotusMode::Emoji: {
                ic_->inputPanel().reset();
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                ic_->updatePreedit();
                break;
            }
            default: {
                break;
            }
        }
    }

    void LotusState::commitBuffer() {
        switch (realMode) {
            case LotusMode::Preedit: {
                ic_->inputPanel().reset();
                if (lotusEngine_) {
                    EngineCommitPreedit(lotusEngine_.handle());
                    UniqueCPtr<char> commit(EnginePullCommit(lotusEngine_.handle()));
                    if (commit && (*commit.get() != 0))
                        ic_->commitString(commit.get());
                }
                ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                ic_->updatePreedit();
                break;
            }
            case LotusMode::Uinput:
            case LotusMode::UinputHC:
            case LotusMode::Smooth: {
                if (lotusEngine_) {
                    UniqueCPtr<char> preedit(EnginePullPreedit(lotusEngine_.handle()));
                    if (preedit && (*preedit.get() != 0)) {
                        ic_->commitString(preedit.get());
                    }
                }
                break;
            }
            case LotusMode::SurroundingText: {
                if (lotusEngine_)
                    ResetEngine(lotusEngine_.handle());
                break;
            }
            default: {
                break;
            }
        }
    }

    void LotusState::clearAllBuffers() {
        LOTUS_DEBUG("Clear all buffers");
        if (is_deleting_.load(std::memory_order_acquire)) {
            return;
        }
        LOG("buf=" + oldPreBuffer_);
        oldPreBuffer_.clear();
        history_.clear();
        if (!is_deleting_.load(std::memory_order_acquire)) {
            expected_backspaces_     = 0;
            current_backspace_count_ = 0;
            pending_commit_string_.clear();
        }
        emojiBuffer_.clear();
        emojiCandidates_.clear();
        buffered_keys_.clear();
        shouldCapitalize_  = false;
        isPrevPunctuation_ = false;
        if (lotusEngine_)
            ResetEngine(lotusEngine_.handle());
    }

    bool LotusState::isEmptyHistory() {
        return history_.empty();
    }
} // namespace fcitx
