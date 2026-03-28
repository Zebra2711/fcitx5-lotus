/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-state.h
 * @brief Input context state management for fcitx5-lotus.
 */

#ifndef _FCITX5_LOTUS_STATE_H_
#define _FCITX5_LOTUS_STATE_H_

#include "lotus.h"
#include "emoji.h"
#include <fcitx-utils/key.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <atomic>
#include <string>
#include <vector>

struct EmojiEntry;

namespace fcitx {
    class LotusEngine;
    class CommonCandidateList;
    class SurroundingText;

    /**
     * @brief Per-input-context state for Lotus input method.
     *
     * Manages the input state, buffers, and mode-specific handling for each input context.
     */
    class LotusState final : public InputContextProperty {
      public:
        /**
         * @brief Constructs a new state instance.
         * @param engine Pointer to the Lotus engine.
         * @param ic Pointer to the input context.
         */
        LotusState(LotusEngine* engine, InputContext* ic);

        /**
         * @brief Destroys the state and releases resources.
         */
        ~LotusState();

        /**
         * @brief Initializes the bamboo engine for this state.
         */
        void setEngine();

        /**
         * @brief Applies current options to the engine.
         */
        void setOption();

        /**
         * @brief Connects to the uinput server.
         * @return True if connection successful.
         */
        bool connect_uinput_server();

        /**
         * @brief Sets up uinput device.
         * @return File descriptor or -1 on error.
         */
        int setup_uinput();

        /**
         * @brief Sends backspace key events via uinput.
         * @param count Number of backspaces to send.
         */
        void send_backspace_uinput(int count);

        /**
         * @brief Replays buffer content to the engine.
         * @param buffer The string buffer to replay.
         */
        void replayBufferToEngine(const std::string& buffer);

        /**
         * @brief Checks if autofill is certain for surrounding text.
         * @param s The surrounding text.
         * @return True if autofill should proceed.
         */
        bool isAutofillCertain(const SurroundingText& s);

        /**
         * @brief Handles key events in preedit mode.
         * @param keyEvent The key event to process.
         */
        void handlePreeditMode(KeyEvent& keyEvent);

        /**
         * @brief Updates emoji page status in candidate list.
         * @param commonList The candidate list to update.
         */
        void updateEmojiPageStatus(CommonCandidateList* commonList);

        /**
         * @brief Handles key events in emoji mode.
         * @param keyEvent The key event to process.
         */
        void handleEmojiMode(KeyEvent& keyEvent);

        /**
         * @brief Selects an emoji candidate by index.
         * @param index Candidate index.
         */
        void selectEmojiCandidate(int index);

        /**
         * @brief Updates preedit display for emoji mode.
         */
        void updateEmojiPreedit();

        /**
         * @brief Handles key press in uinput mode.
         * @param event The key event.
         * @param currentSym Current key symbol.
         * @param sleepTime Delay in microseconds.
         * @return True if event was handled.
         */
        bool handleUInputKeyPress(KeyEvent& event, KeySym currentSym, int sleepTime);

        /**
         * @brief Performs text replacement via uinput.
         * @param deletedPart Text to delete.
         * @param addedPart Text to insert.
         */
        void performReplacement(const std::string& deletedPart, const std::string& addedPart);

        /**
         * @brief Checks and forwards special keys.
         * @param keyEvent The key event.
         * @param currentSym Current key symbol (may be modified).
         */
        void checkForwardSpecialKey(KeyEvent& keyEvent, KeySym& currentSym);

        /**
         * @brief Handles uinput mode processing.
         * @param keyEvent The key event.
         * @param currentSym Current key symbol.
         * @param checkEmptyPreedit Whether to check for empty preedit.
         * @param sleepTime Delay in microseconds.
         */
        void handleUinputMode(KeyEvent& keyEvent, KeySym currentSym, bool checkEmptyPreedit, int sleepTime);

        /**
         * @brief Handles surrounding text mode.
         * @param keyEvent The key event.
         * @param currentSym Current key symbol.
         */
        void handleSurroundingText(KeyEvent& keyEvent, KeySym currentSym);

        /**
         * @brief Main key event handler.
         * @param keyEvent The key event to process.
         */
        void keyEvent(KeyEvent& keyEvent);

        /**
         * @brief Resets the input state.
         */
        void reset();

        /**
         * @brief Commits the current buffer.
         */
        void commitBuffer();

        /**
         * @brief Clears all internal buffers.
         */
        void clearAllBuffers();

        /**
         * @brief Checks if history buffer is empty.
         * @return True if no history.
         */
        bool isEmptyHistory();

        friend class EmojiCandidateWord;
        friend class LotusEngine;

      private:
        LotusEngine*            engine_;
        InputContext*           ic_;
        CGoObject               lotusEngine_;
        std::string             oldPreBuffer_;
        std::string             history_;
        size_t                  expected_backspaces_     = 0;
        size_t                  current_backspace_count_ = 0;
        std::string             pending_commit_string_;
        std::atomic<int>        current_thread_id_{0};
        std::string             emojiBuffer_;
        std::vector<EmojiEntry> emojiCandidates_;
        bool                    waitAck_           = false;
        size_t                  prevSurrSuffixLen_ = 0; ///< Previous suffix length
    };

} // namespace fcitx

#endif // _FCITX5_LOTUS_STATE_H_
