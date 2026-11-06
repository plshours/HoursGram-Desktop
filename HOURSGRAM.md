# HoursGram Desktop

This folder is the native Windows client source. It is based on AyuGram Desktop 7.0.9 and Telegram Desktop, so the interface is implemented with the native C++/Qt desktop UI rather than the web client in a Tauri window.

The executable target is named `HoursGram.exe`. Its app ID and profile folders use HoursGram names, keeping its settings and account data separate from Telegram Desktop and other clients.

The About window links to this HoursGram repository. The upstream `LICENSE` and copyright notices remain in this source tree; the first-launch notice and this document identify AyuGram Desktop as the source project.

The existing TypeScript web client remains in the parent folder.

## Plugins

HorusGram plugins are `.hgplugin` files: one JavaScript file that runs the same in HorusGram for Android and here. What a plugin can use is described in `PLUGINS.md` of the Android repository (https://github.com/plshours/HorusGram---Android).

- Install: open a `.hgplugin` file sent to any chat, or HorusGram Preferences → Plugins → Install from file.
- Code: `Telegram/SourceFiles/hoursgram/plugins`. Outgoing text goes through the plugins in `ApiWrap::sendMessage`, new incoming messages in `History::newItemAdded`.
- Engine: QuickJS-ng, copied into `Telegram/ThirdParty/quickjs` and built by `Telegram/cmake/lib_quickjs.cmake`. A plugin gets the standard JavaScript objects and the `HG` object only, 2 seconds per call and 64 MB of memory.
- `Telegram/Resources/hoursgram/horus_plugin_prelude.js` builds `HG` on top of the client. It is the same file as `TMessagesProj/src/main/assets/horus_plugin_prelude.js` in the Android repository; change both together, or a plugin stops behaving the same on both.
- Installed plugins, their settings and storage are kept in `tdata/hoursgram_plugins`.
