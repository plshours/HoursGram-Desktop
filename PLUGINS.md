# HorusGram plugins

A HorusGram plugin is one JavaScript file with the extension `.hgplugin`. The
same file runs in HorusGram for Android and in HorusGram Desktop: both clients
give it the same `HG` object, described here, and nothing else.

To install a plugin, send the `.hgplugin` file to any chat (Saved Messages
works) and open it in HorusGram. The client shows what the plugin is and what it
will be allowed to do, and installs it on confirmation. Installed plugins are
listed under Settings → HorusGram → Plugins, where they can be turned off,
set up and deleted.

## The file

```js
// ==HorusGramPlugin==
// @id          example.uppercase
// @name        Uppercase
// @version     1.0
// @author      someone
// @description Shouts: .up text sends the text in capitals.
// ==/HorusGramPlugin==

HG.commands.register("up", function (args, chat) {
    return args.toUpperCase();
}, "Send the text in capitals");
```

The header is required. `@id` is unique: letters, digits, dots, dashes and
underscores. Installing a plugin with an id that is already installed replaces
it, keeping its settings and storage.

The script runs once when the plugin is turned on. It registers what it
reacts to; the client calls it later. A plugin gets 2 seconds of work per
call, after which the call is stopped.

## `HG`

| | |
|---|---|
| `HG.apiVersion` | `1` |
| `HG.platform` | `"android"` or `"desktop"` |
| `HG.commands.register(name, fn, description)` | A command: a message `.name args` runs `fn(args, chat)`. A string it returns is sent in place of the command; `null` or nothing sends nothing. |
| `HG.on("outgoingMessage", fn)` | `fn(message)` before a text message is sent. Return a string to send it instead, `false` to not send at all, nothing to leave the message as it is. |
| `HG.on("incomingMessage", fn)` | `fn(message)` for every new incoming message, after it arrives. The return value is ignored. |
| `HG.settings.declare(list)` | The settings shown for the plugin; see below. |
| `HG.settings.get(key)` | The value of a setting, or its default. |
| `HG.storage.get(key, fallback)` / `HG.storage.set(key, value)` | Values kept between runs, for this plugin only. Anything JSON can hold. |
| `HG.sendMessage(chatId, text)` | Sends a text message to a chat. |
| `HG.toast(text)` | A short notice on the screen. |
| `HG.log(text)` | A line in the client's log. |

`message` and `chat` are plain objects:

```js
message = { text: "hello", chatId: "123456789", fromId: "123456789", out: false }
chat    = { chatId: "123456789" }
```

Chat and user ids are strings, so no precision is lost to JavaScript numbers.

## Settings

```js
HG.settings.declare([
    { type: "header", title: "Replies" },
    { type: "switch", key: "enabled", title: "Answer when away", default: false },
    { type: "text",   key: "reply",   title: "Answer", default: "I'm away" },
    { type: "info",   title: "Answers private chats only." }
]);
```

Types: `header`, `switch` (true or false), `text` (a string), `info` (a line of
explanation).

## What a plugin cannot do

A plugin sees only `HG` and the standard JavaScript objects. It has no access
to files, the network, other plugins, or the inner workings of the client, so
installing one cannot do more than the dialog says.
