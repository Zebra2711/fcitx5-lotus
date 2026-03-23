#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Application entry point.
"""

import sys
import signal
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QTimer
from PySide6.QtGui import QIcon
from i18n import setup_i18n
from ui.main_window import LotusSettingsWindow


def main():
    """Main execution function."""
    setup_i18n()
    app = QApplication(sys.argv)
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    app.setWindowIcon(QIcon.fromTheme("fcitx-lotus"))

    timer = QTimer()
    timer.start(500)
    timer.timeout.connect(lambda: None)

    window = LotusSettingsWindow()
    window.show()

    try:
        sys.exit(app.exec())
    except KeyboardInterrupt:
        app.quit()


if __name__ == "__main__":
    main()
