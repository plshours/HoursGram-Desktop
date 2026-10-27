# HoursGram Desktop

HoursGram Desktop is a native C++/Qt Telegram client fork based on AyuGram Desktop 7.0.9 and Telegram Desktop. The application and its Windows package are branded as HoursGram.

## Windows build

The GitHub Actions workflow builds the x64 Windows client in **Debug** for pull requests and pushes to `main`, then uploads the `HoursGram-Windows-x64-Debug` artifact. Debug is the fast configuration for iteration. To make an optimized build, open **Actions → HoursGram Desktop Windows Build → Run workflow** and choose **Release**. Builds are not published to GitHub Releases automatically.

For a local build, follow [the Windows build guide](docs/building-win.md).

```powershell
git clone --recursive https://github.com/sixseven67123/HoursGram-Desktop.git hoursgram
```

## Source and attribution

HoursGram is an unofficial Telegram client. This repository preserves the original license and copyright notices for AyuGram Desktop and Telegram Desktop. See [LICENSE](LICENSE) and the upstream projects: [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop) and [Telegram Desktop](https://github.com/telegramdesktop/tdesktop).

The rounded profile sections come from mokaGram and are used under GPLv3 with the attribution term of its notice, kept here as [MOKAGRAM_NOTICE.md](MOKAGRAM_NOTICE.md). The code is modified: the corners are filled with this client's own panel colour and the metrics come from its style files.

**Based on mokaGram by @ecronx (https://t.me/ecronx). https://github.com/ecronxy/mokagram**
