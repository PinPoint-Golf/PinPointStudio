// Fake TtsController (src/Gui/media/tts_controller.h) — the voice TrunkRoutine's EXPERIMENTAL spoken
// instruction goes through (27 Oct 2026). It only RECORDS: no synthesis, no audio device, no sound —
// ever (tests must be silent; the real TtsController is not even compiled into session_setup_ui_test).
//
//   calls     every call, oldest first: { name: "speak" | "stopSpeaking" | "prepare", text, t };
//             called(name, text) fires with each, so a case can note where the routine was at that
//             instant. isPrepared() is a query and is NOT recorded.
//   spoken()  the texts speak() was given; preparedTexts() the texts prepare() was given
//   ttsReady / ttsActive / lastTtsLatencyMs / voice  as the real controller's properties; a case sets them
//   finish(ms)  what the real one does when a synthesis ends: lastTtsLatencyMs = ms (its signal fires)
//
// PREPARED PHRASES, as the real controller keeps them: prepare(text) while ttsReady marks the text
// prepared at once (the fake's synthesis is instant); prepare() while not ready is recorded but
// prepares nothing; a voice change empties the cache BEFORE anyone else hears voiceChanged (the real
// setVoice invalidates first) — this object's own handler is connected first, so it runs first.
import QtQuick

QtObject {
    property bool   ttsReady: true
    property bool   ttsActive: false
    property real   lastTtsLatencyMs: 0
    property string voice: "af_sky"
    property var    _cache: ({})            // text → true (prepared)
    property var    calls: []
    signal called(string name, string text)
    signal preparedChanged()

    onVoiceChanged: { _cache = ({}); preparedChanged() }

    function speak(text) { calls.push({ name: "speak", text: text, t: Date.now() }); called("speak", text) }
    function stopSpeaking() { calls.push({ name: "stopSpeaking", text: "", t: Date.now() }); called("stopSpeaking", "") }
    function prepare(text) {
        calls.push({ name: "prepare", text: text, t: Date.now() })
        called("prepare", text)
        if (!ttsReady || _cache[text] === true) return
        var p = {}
        for (var k in _cache) p[k] = _cache[k]
        p[text] = true
        _cache = p
        preparedChanged()
    }
    function isPrepared(text) { return _cache[text] === true }

    function spoken() {
        return calls.filter(function(c) { return c.name === "speak" }).map(function(c) { return c.text })
    }
    function preparedTexts() {
        return calls.filter(function(c) { return c.name === "prepare" }).map(function(c) { return c.text })
    }
    function count(name) { return calls.filter(function(c) { return c.name === name }).length }
    function finish(ms) { lastTtsLatencyMs = ms }
    function reset() { calls = []; ttsReady = true; ttsActive = false; lastTtsLatencyMs = 0; _cache = ({}) }
}
