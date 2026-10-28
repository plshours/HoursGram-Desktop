# HoursGram Desktop

<p align="center">
  <a href="https://github.com/plshours/HoursGram-Desktop/actions/workflows/native-windows.yml"><img src="https://github.com/plshours/HoursGram-Desktop/actions/workflows/native-windows.yml/badge.svg" alt="Windows build"></a>
  <a href="https://github.com/plshours/HoursGram-Desktop/actions/workflows/native-linux.yml"><img src="https://github.com/plshours/HoursGram-Desktop/actions/workflows/native-linux.yml/badge.svg" alt="Linux build"></a>
  <img src="https://img.shields.io/badge/version-1.0.0-8993D8?style=flat-square" alt="Version 1.0.0">
  <img src="https://img.shields.io/badge/license-GPLv3-8993D8?style=flat-square" alt="License GPLv3">
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white" alt="C++20">
  <img src="https://img.shields.io/badge/Qt-6-41CD52?style=flat-square&logo=qt&logoColor=white" alt="Qt 6">
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-8993D8?style=flat-square" alt="Platform">
</p>

HoursGram Desktop is an **unofficial** Telegram client, a fork of [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop), which is itself a fork of [Telegram Desktop](https://github.com/telegramdesktop/tdesktop). It is **not affiliated with, endorsed by or sponsored by Telegram FZ-LLC or Telegram Messenger Inc.** "Telegram" is a trademark of its owner and is used here only to say what this client talks to.

## Windows build

The GitHub Actions workflow builds the x64 Windows client in **Debug** for pull requests and pushes to `main`, then uploads the `HoursGram-Windows-x64-Debug` artifact. To make an optimized build, open **Actions → HoursGram Desktop Windows Build → Run workflow** and choose **Release**. Builds are not published to GitHub Releases automatically.

For a local build, follow [the Windows build guide](docs/building-win.md). Linux and macOS: [Linux](docs/building-linux.md), [macOS](docs/building-mac.md).

```powershell
git clone --recursive https://github.com/plshours/HoursGram-Desktop.git hoursgram
```

## Source, licence and attribution

This repository is a derivative work and keeps the notices of the projects it is built from. Nothing below was written by the people named, and they have not approved this client.

| Project | Author | Licence |
|---|---|---|
| [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) | © The Telegram Desktop Authors | GPLv3 with the OpenSSL exception ([LICENSE](LICENSE), [LEGAL](LEGAL)) |
| [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop) | © Radolyn | GPLv3 |
| [mokaGram](https://github.com/ecronxy/mokagram) | © @ecronx | GPLv3 with an attribution term ([MOKAGRAM_NOTICE.md](MOKAGRAM_NOTICE.md)) |
| [Telegram for macOS](https://github.com/TelegramMessenger/telegram-macos) | © Telegram FZ-LLC | GPL-2.0; only its colour values are used, for the HorusGram macOS theme |
| [Montserrat](https://github.com/JulietaUla/Montserrat) | © The Montserrat Project Authors | SIL OFL 1.1 ([OFL.txt](Telegram/Resources/fonts/OFL.txt)) |

- The copyright header of every file taken from those projects is kept as it was. Files written for HoursGram live in `Telegram/SourceFiles/hoursgram/` and carry their own header.
- HoursGram is licensed under **GPLv3**, the same terms. The complete source of every build is this repository, at the commit the build names.
- What was changed is recorded in the git history and summarised in [NOTICE.md](NOTICE.md).
- Telegram requires a client to use its own API credentials. Set `TDESKTOP_API_ID` and `TDESKTOP_API_HASH` (from <https://my.telegram.org>) as repository secrets before distributing builds; see [NOTICE.md](NOTICE.md).

**Based on mokaGram by @ecronx (https://t.me/ecronx). https://github.com/ecronxy/mokagram**
