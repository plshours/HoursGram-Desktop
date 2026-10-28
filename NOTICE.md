# Notice

HoursGram Desktop is a modified version of other people's free software. This file says whose, what was changed, and how to reach us.

## Where this code comes from

1. **Telegram Desktop** — https://github.com/telegramdesktop/tdesktop  
   Copyright (c) The Telegram Desktop Authors. GNU GPL version 3 with the OpenSSL exception. See [LICENSE](LICENSE) and [LEGAL](LEGAL).
2. **AyuGram Desktop** — https://github.com/AyuGram/AyuGramDesktop  
   Copyright (c) Radolyn. GNU GPL version 3. Its sources are in `Telegram/SourceFiles/ayu/` and keep the original headers.
3. **mokaGram** — https://github.com/ecronxy/mokagram  
   Copyright (c) 2026 @ecronx. GPLv3 with an attribution term, kept in [MOKAGRAM_NOTICE.md](MOKAGRAM_NOTICE.md).
4. **Montserrat** (fonts) — Copyright (c) The Montserrat Project Authors. SIL Open Font License 1.1, text in `Telegram/Resources/fonts/OFL.txt`. The font files are used unmodified.

Third-party libraries built with the client keep their own licences; they are listed in the upstream `Telegram/ThirdParty` and `cmake` directories.

## What HoursGram changed

Dates are those of the commits in the git history, from October 2026.

- the interface: palette, rounded surfaces, the Montserrat fonts, the chat input bar and background;
- a music player with playlists and line-by-line lyrics (`Telegram/SourceFiles/hoursgram/music/`);
- a Customization section in settings (`Telegram/SourceFiles/ayu/ui/settings/`);
- build files for Windows and Linux (`.github/`), packaging for Arch and Manjaro;
- the name, icons and version number.

Every file changed or added keeps the header of its origin. Files that are entirely new carry the HoursGram header.

## Licence of this work

HoursGram is distributed under the **GNU General Public License version 3**, with the OpenSSL exception inherited from Telegram Desktop. You may use, study, change and share it under those terms. The complete corresponding source of every binary is this repository, at the commit named by the build.

This program comes with **no warranty**, to the extent permitted by law.

## Not Telegram

HoursGram is an independent, unofficial client. It is not made, approved or supported by Telegram FZ-LLC or Telegram Messenger Inc. Telegram is a trademark of its owner. The Telegram logo is not used as the logo of this project.

## Telegram API credentials

Telegram asks every client to use its own `api_id` and `api_hash`, issued at <https://my.telegram.org>. The workflows read them from the repository secrets `TDESKTOP_API_ID` and `TDESKTOP_API_HASH`. Until those are set, they fall back to the public development values from Telegram Desktop's build documentation, which are meant for building and testing only and should not be used for a build you hand to other people.

## HorusGram macOS theme

The colours of `Telegram/Resources/hoursgram-macos.tdesktop-palette` are the values of the light palette of [Telegram for macOS](https://github.com/TelegramMessenger/telegram-macos) (GPL-2.0, © Telegram FZ-LLC). Only colour values are taken, no code, no images and no fonts.

## If you are an author and something is missing

If you wrote part of this code and think your credit, your licence or your terms are not respected here, open an issue on this repository or write to the maintainer through the profile page. We will correct it quickly, and remove the code if you ask us to.
