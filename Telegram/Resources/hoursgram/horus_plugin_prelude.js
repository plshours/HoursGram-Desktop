// HorusGram plugin runtime, shared by HorusGram for Android and HorusGram
// Desktop: both run this before a plugin, with `__hgBridge` set to their own
// way into the client, so a plugin meets the same `HG` on either. See
// PLUGINS.md. Values cross the bridge as JSON strings, which both engines
// pass without surprises.
var __hgRuntime = (function (bridge) {
    var handlers = {};
    var commands = {};
    var schema = [];

    function decode(value) {
        return value === null || value === undefined ? undefined : JSON.parse(String(value));
    }

    function defaultOf(key) {
        for (var i = 0; i < schema.length; i++) {
            if (schema[i] && schema[i].key === key) {
                return schema[i]["default"];
            }
        }
        return undefined;
    }

    var HG = {
        apiVersion: 1,
        platform: String(bridge.platform()),
        commands: {
            register: function (name, fn, description) {
                if (typeof fn !== "function") {
                    throw new TypeError("HG.commands.register needs a function");
                }
                var key = String(name).replace(/^\./, "").toLowerCase();
                commands[key] = { fn: fn, description: description ? String(description) : "" };
            }
        },
        on: function (event, fn) {
            if (typeof fn !== "function") {
                throw new TypeError("HG.on needs a function");
            }
            (handlers[event] = handlers[event] || []).push(fn);
        },
        settings: {
            declare: function (list) {
                schema = Array.isArray(list) ? list : [];
                bridge.declareSettings(JSON.stringify(schema));
            },
            get: function (key) {
                var value = decode(bridge.getSetting(String(key)));
                return value === undefined ? defaultOf(String(key)) : value;
            }
        },
        storage: {
            get: function (key, fallback) {
                var value = decode(bridge.storageGet(String(key)));
                return value === undefined ? fallback : value;
            },
            set: function (key, value) {
                bridge.storageSet(String(key), JSON.stringify(value === undefined ? null : value));
            }
        },
        sendMessage: function (chatId, text) {
            bridge.sendMessage(String(chatId), String(text));
        },
        toast: function (text) {
            bridge.toast(String(text));
        },
        log: function (text) {
            bridge.log(String(text));
        }
    };

    return {
        HG: HG,
        // {"text": ...} to send another text, {"cancel": true} to send
        // nothing, {} to leave the message alone.
        dispatch: function (event, json) {
            var list = handlers[event] || [];
            var message = JSON.parse(json);
            var result = {};
            for (var i = 0; i < list.length; i++) {
                var answer = list[i](message);
                if (event === "outgoingMessage") {
                    if (answer === false) {
                        return JSON.stringify({ cancel: true });
                    }
                    if (typeof answer === "string") {
                        message.text = answer;
                        result.text = answer;
                    }
                }
            }
            return JSON.stringify(result);
        },
        command: function (name, args, json) {
            var command = commands[name];
            if (!command) {
                return JSON.stringify({ missing: true });
            }
            var answer = command.fn(args, JSON.parse(json));
            return JSON.stringify(typeof answer === "string" ? { text: answer } : { cancel: true });
        },
        commands: function () {
            var list = [];
            for (var name in commands) {
                list.push({ name: name, description: commands[name].description });
            }
            return JSON.stringify(list);
        }
    };
})(__hgBridge);
var HG = __hgRuntime.HG;
