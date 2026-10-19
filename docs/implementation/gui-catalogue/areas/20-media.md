# Media (dev hatch)

*Directory:* `src/Gui/media/`  
*3 components · 1,495 lines of QML*

Audio (STT/TTS), AI Coach and Film pages; only reachable through PlayPage's developer hatch (Film not at all).

**C++ in this area:** `FilmController`, `LlmController`, `TranscriptionController`, `TtsController`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [AudioPage](#audiopage) | 483 | `Item` | 1 | 1 |  |
| [CoachPage](#coachpage) | 415 | `Item` | 1 | 0 |  |
| [FilmPage](#filmpage) | 597 | `Item` | 0 | 1 | ⚠ unreferenced |

### AudioPage
`src/Gui/media/AudioPage.qml` · 483 lines · root `Item` · 8 hard-coded colour(s)

Audio page (dev hatch): live speech-to-text transcript pane (CPU, GPU or Azure cloud) and a text-to-speech pane, in a vertical SplitView; model-download progress.  
*(description written for this catalogue — the file has no header comment)*

- **Uses:** `PpComboBox`
- **Used by (1):** `PlayPage`

### CoachPage
`src/Gui/media/CoachPage.qml` · 415 lines · root `Item` · 7 hard-coded colour(s)

AI Coach chat page: local LLM or cloud (Gemini) backend toggle, download/loading overlay, latency badge, chat transcript, voice-in and voice-out toggles. Hosted by PlayPage (dev hatch).  
*(description written for this catalogue — the file has no header comment)*

- **Functions:** `sendAndClear()`
- **Used by (1):** `PlayPage`

### FilmPage
`src/Gui/media/FilmPage.qml` · 597 lines · root `Item` · ⚠ **no app file instantiates this** · 12 hard-coded colour(s)

Film page: paste a YouTube URL, download via yt-dlp, a cache list, and playback with 10 s skip, pause/stop and an Annotate mode. NOTE: nothing instantiates this file (PlayPage no longer lists it).  
*(description written for this catalogue — the file has no header comment)*

- **Functions:** `formatTime(ms)`
- **Uses:** `PpComboBox`
