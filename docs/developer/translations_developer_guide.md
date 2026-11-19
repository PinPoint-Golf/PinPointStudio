# PinPoint Studio — Translations Developer Guide

**Audience**: Developers adding a language, refreshing a translation, or writing interface text
**Location**: `src/Resources/i18n/` (the catalogues), `cmake/PinPointTranslations.cmake` (the one list of them), `src/Gui/app/app_translations.h` (the loader), `src/Gui/settings/GeneralPanel.qml` (the Language setting), `src/Gui/tests/app_translations_test.cpp`
**Language**: Qt Linguist `.ts` (XML), CMake, C++, QML
**Status**: Every language the Language setting offers has a catalogue: French (`fr_FR`), German (`de_DE`), Japanese (`ja_JP`), Korean (`ko_KR`) and Simplified Chinese (`zh_CN`). The Chinese catalogue was reviewed by native-speaker passes and a 600-message sample audit; the other four have had one review pass per section and the mechanical checks in §7, and would benefit from a fluent golfer reading them in the running app.

---

## Contents

1. [How a language reaches the screen](#1-how-a-language-reaches-the-screen)
2. [What is translated, and what is not yet](#2-what-is-translated-and-what-is-not-yet)
3. [Adding a language](#3-adding-a-language)
4. [Refreshing a catalogue when the source changes](#4-refreshing-a-catalogue-when-the-source-changes)
5. [Writing text that can be translated](#5-writing-text-that-can-be-translated)
6. [Why the language does not change the locale](#6-why-the-language-does-not-change-the-locale)
7. [The test](#7-the-test)
8. [Simplified Chinese: the conventions in use](#8-simplified-chinese-the-conventions-in-use)
9. [The other languages: the choices that matter](#9-the-other-languages-the-choices-that-matter)

---

## 1. How a language reaches the screen

| Step | Where | What happens |
|---|---|---|
| The choice | `GeneralPanel.qml`, `languageModel` | The combo writes a tag (`zh_CN`) to `AppSettings.language`, stored as `General/language`. The default is `en_GB`. |
| The load | `main.cpp`, straight after `AppSettings` is constructed | `AppTranslations::install(app, appSettings.language())` looks for `:/i18n/PinPointStudio_<tag>.qm`, then for the bare language (`_zh`), and installs the first that exists. One line in the log says which, or warns that the language has no catalogue. |
| The catalogue | `src/Resources/i18n/PinPointStudio_<tag>.ts` | One file per language, listed in `cmake/PinPointTranslations.cmake`. |
| The build | root `CMakeLists.txt`, `qt_add_translations` | `lrelease` compiles each `.ts` to a `.qm` on every build and embeds it in the executable at `:/i18n`. Nothing is added to an installer, DMG or AppImage. |

The source text is British English, so `en_GB` and `en_US` load nothing.

**It applies on restart, and that is deliberate.** A translator installed after the interface exists
reaches `qsTr()` bindings only on a retranslate, and never reaches text a C++ model built with
`tr()` and kept. The loader therefore runs once, before any controller or the QML engine exists,
and the setting's subtitle says "Restart required to apply".

`LinguistTools` is a required Qt component (`find_package` in the root `CMakeLists.txt`). It ships
with the base Qt kit on every platform; it is not an installer add-on.

## 2. What is translated, and what is not yet

A catalogue covers every string that goes through `qsTr()` in QML and `tr()` /
`QCoreApplication::translate()` in C++: about 3,800 messages. That is the application's chrome —
Settings, session setup, Home's frame, athletes, the session lists, shot review, the cameras and
sensors pages, the resource monitor and the diagnostic-model editor.

It does **not** cover text that never passes through Qt translation. With a language selected,
these stay in English:

| What | Where it comes from |
|---|---|
| Metric names, groups, descriptions and "how to read" text | `src/Metrics/metric_catalogue_manifest.cpp` (plain `QStringLiteral`) |
| Swing phase names on the timeline | `src/Gui/review/timeline_labels.cpp` |
| Habits, causes, cues, drills and screens | The diagnostics packs, `src/Resources/diagnostics/*.json`. Narrative fields are keyed by locale (`LocalisedText`), and only `"en"` is written or read. |
| The Charts panel's sentences | `src/Gui/charts/*_presenter.cpp`, composed in C++ with English grammar |
| The Diagnostics panel and the session report | `src/Gui/diagnostics/session_diagnostics_model.cpp`, `src/Gui/report/report_words.h`, and their QML pages, most of which use bare string literals |
| Home's coaching sentences and tendencies | `src/Coaching/*.h` |
| Dates and thousands separators | Formatted with an explicit English or C locale (see §6) |
| Qt's own text (file dialogs, standard buttons) | Qt's catalogues are not deployed (`windeployqt --no-translations`) |

Translating those is a separate piece of work for each source: the C++ sentence builders need
`tr()` and a design that does not assume English word order, and the packs need a second locale
key. Until then a translated interface is a translated frame around English analysis, and a
release note for a new language should say so.

## 3. Adding a language

1. **Name the file for the tag.** `src/Resources/i18n/PinPointStudio_<tag>.ts`, where `<tag>` is
   exactly what the Language combo stores (`ja_JP`). The loader finds a catalogue by that name and
   no other way.
2. **List it** in `PP_TS_FILES` in `cmake/PinPointTranslations.cmake`. The app and
   `app_translations_test` both read that list.
3. **Create it** with `cmake --build <build> --target update_translations`. Qt writes an empty
   catalogue for a listed file that does not exist, then lupdate fills it with every source string.
4. **Offer it**, if the combo does not already: add `{ label: qsTr("…"), tag: "<tag>" }` to
   `languageModel` in `GeneralPanel.qml`. The label is the language's own name and is never
   translated.
5. **Translate it** in Qt Linguist or as XML. A message is finished when its `<translation>` has no
   `type="unfinished"`. For a plural message give as many `<numerusform>` entries as the language has
   plural forms (one for Chinese, Japanese and Korean).
6. **Run the test** (§7). It checks the new catalogue with no change to the test.

## 4. Refreshing a catalogue when the source changes

```
cmake --build <build> --target update_translations
```

This runs lupdate over the `PinPointStudio` target's sources — C++, and the QML and JS files of its
QML module — and merges the result into each `.ts`. New strings arrive as `type="unfinished"`,
strings that have gone are marked `type="vanished"`, and existing translations are kept. A normal
build never runs it: the `.ts` files are committed, and only `lrelease` runs at build time.

Three things to know:

- **Platform-only sources.** A target's sources are the ones this platform compiles. The files that
  carry `tr()` and are compiled only elsewhere (the AppImage updater, the macOS and Spinnaker camera
  backends, the phone link) are named in `PP_TS_EXTRA_SOURCES` so that a refresh on any machine
  still sees them. When you put `tr()` in a file that the root `CMakeLists.txt` adds inside an
  `if()`, add it to that list.
- **No file or line numbers.** lupdate runs with `-locations none`. With locations, every edit above
  a string would rewrite its entry and bury real changes in the diff.
- **Read lupdate's warnings.** "tr() cannot be called without context" and "Class … lacks Q_OBJECT
  macro" both mean a string was filed under a context the running code will never ask for; see §5.

After a refresh, translate the unfinished entries and run the test. An unfinished entry fails it.

## 5. Writing text that can be translated

The design system's content rules (`docs/design/system/README.md`) apply to every language:
second person, plain words, sentence case in the source with `Theme.caps(qsTr(...))` for headings,
acronyms in capitals, `.arg()` after `caps()`. The rules below are the ones that only show once
there is a second language. Each was a live defect when the first catalogue was added.

**A stored value is never the translated word.** A chip, combo or segment that writes to
`AppSettings` or to a swing's record stores a fixed token and *shows* a translation of it:

```qml
PpChipGroup {
    options:  [{ label: qsTr("Low"),  value: "Low" },
               { label: qsTr("High"), value: "High" }]
    selected: appSettings.swingDetectionSensitivity
}
```

`options: [qsTr("Low"), qsTr("High")]` stores whatever the label says. C++ compares the setting
with `"Low"`, so under a translation it matches nothing and the behaviour changes with no error.

**Compare and search tokens, not sentences.** Do not test a message for an English phrase, trim it
with a regular expression, or compare a `tr()` string from one class with a `tr()` string from
another: they are separate catalogue entries and a translator need not make them agree. Hand the
consumer a field (`where: "studio"`) or a second, complete sentence (`StudioProfile::missingDetails`
gives both `text` and `brief`; `ArchiveController` has a `failed` property beside its `status`
words). One exception is checked by the test because the interface relies
on it: every message that names "Settings → Studio" must use the same words for it.

**Mark and look up under one context.** `QT_TR_NOOP("…")` files a string under the enclosing class;
the later `tr(x)` must be that class's. In a free function, or when the lookup is `QObject::tr`, say
the context: `QT_TRANSLATE_NOOP("QObject", "…")`. A `QT_TRANSLATE_NOOP("Ctx", …)` string is
translated only where something calls `QCoreApplication::translate("Ctx", x)`.

**Do not wrap `translate()` in a local `tr`.** lupdate reads the call, not what a lambda forwards
to: `auto tr = [](const char *s) { return QCoreApplication::translate("ReportMailer", s); }` files
every string under the namespace, and none is ever found. Write
`QCoreApplication::translate("ReportMailer", "…")` at each string.

**Whole sentences, with placeholders.** `qsTr("%1 of %2 swings").arg(a).arg(b)`, not
`a + qsTr(" of ") + b + qsTr(" swings")`. Word order differs between languages, and a fragment
gives a translator nothing to reorder. Where a fragment is unavoidable, its leading or trailing
space is part of the string and the test checks a translation keeps it.

**Plurals use `%n`.** `qsTr("%n session(s)", "", n)` lets each language supply its own forms.
`Theme.plural(n, one, many)` and `n === 1 ? a : b` are English's two forms and no other language's.

**Say which sense you mean.** A one-word string that could be read two ways needs a translator
comment, `//: P4, the top of the backswing` on the line above the `qsTr()`.

## 6. Why the language does not change the locale

`AppTranslations` installs a translator and does not call `QLocale::setDefault()`. Every date,
figure and file name in the application names its locale explicitly (`QLocale::c()`,
`QLocale(QLocale::English, …)`, ISO formats, `QString::number`), so a default locale would change
almost nothing that is displayed — but it would change what the decimal fields accept. The loft and
exposure fields validate with a `DoubleValidator`, which follows the default locale, and parse with
`parseFloat`, which does not: under a default locale with a decimal comma, `34,5` passes the
validator and is stored as `34`. Localising dates and figures therefore means replacing those
explicit locales and pinning the validators, as one piece of work, not setting a default.

`app_translations_test` asserts that installing a language leaves `QLocale()` as it was.

## 7. The test

`app_translations_test` (`src/Gui/tests`) compiles the same `.ts` files into itself, at the same
`:/i18n` path, and drives the app's loader.

```
cmake -S src/Gui/tests -B build/gui-tests
cmake --build build/gui-tests --target app_translations_test
ctest --test-dir build/gui-tests -R app_translations --output-on-failure
```

It checks the loader (English loads nothing; a language with no catalogue, or a tag that is not a
tag, installs nothing; a territory falls back to its language and no further; the default locale is
untouched) and then every catalogue in the list:

- no message is unfinished or empty;
- each translation has its source's placeholders (`%1`, `%n`), line breaks, and leading and trailing
  spaces;
- "Settings → Studio" is said one way everywhere;
- the seven language names are left alone, and so are the spoken calibration lines (§8);
- the compiled `.qm` answers every message with the `.ts`'s translation, so `lrelease` dropped
  nothing and the loader installed that file.

It cannot see a string the code looks up under a context lupdate never filed it under: that entry
is absent, not wrong. lupdate's warnings (§4) are the check for that.

It also prints, without failing, any language the combo offers that has no catalogue (none today).

## 8. Simplified Chinese: the conventions in use

`PinPointStudio_zh_CN.ts` was translated to these rules; keep to them when adding to it.

- 你, never 您. No exclamation marks the source does not have. A label has no full stop; a sentence
  ends in 。
- One half-width space between Chinese and Latin letters, digits or placeholders (`使用 IMU 传感器`,
  `约 %1 秒`); none beside full-width punctuation.
- **lead / trail** are 前导 / 后随, never 左 / 右: the app is handedness-neutral.
- **Fade, Draw, Slice, Hook, Push, Pull** stay in English. Their Chinese names (左曲, 右曲, 左拉, 右推)
  name a side and are wrong for a left-handed golfer.
- Product, device and library names, acronyms (IMU, DTL, P1…P10) and unit symbols are not translated.
- "Settings → Studio" is always `设置 → 工作室`. A settings path or a button named inside a sentence
  goes in Chinese quotation marks: 请在“设置 → 工作室”中添加, 点击“重新校准”.
- Something in progress reads 正在…: `正在连接…`, with the source's single `…`. A counter is
  `%1/%2`; a page or step is `第 %1/%2 步`. "e.g." is `例如：`. An on-screen control is 点击, a key is 按.
- The three lines in `spoken_instructions.js` are left in English: the trunk routine speaks them,
  and both voices (espeak `en-us`, Azure `en-GB`) read only English.

Core terms: session 训练 · shot 击球 · swing 挥杆 · athlete 球员 · studio 工作室 · launch monitor
弹道测量仪 · address 准备站位 · backswing 上杆 · top 上杆顶点 · downswing 下杆 · impact 击球瞬间 ·
follow-through 送杆 · finish 收杆 · tempo 节奏 · kinematic sequence 运动链顺序 · club path 杆头路径 ·
face angle 杆面角 · attack angle 攻角 · dynamic loft 动态杆面倾角 · strike 触球 · norm 常模 ·
corridor 参考区间 · characteristic 特征 · measure 测量项 · metric 指标 · drill 练习 ·
face-on 正面 · down the line 目标线后方.

## 9. The other languages: the choices that matter

Each catalogue was written to its own guide. These are the decisions a reviewer is most likely to
question, and the ones to keep when adding to a catalogue.

| | French | German | Japanese | Korean |
|---|---|---|---|---|
| Address the user | vous | du | no pronoun; です／ます in sentences | no pronoun; 합니다체, -(으)세요 for instructions |
| "Settings → Studio" | `Paramètres → Studio` | `Einstellungen → Studio` | `設定 → スタジオ` | `설정 → 스튜디오` |
| lead / trail | avant / arrière | vorder- / hinter- | リード側 / トレイル側 | 리드 / 트레일 |
| Fade, Draw, Slice, Hook, Push, Pull | kept in English | kept in English | フェード, ドロー, スライス, フック, プッシュ, プル | 페이드, 드로우, 슬라이스, 훅, 푸시, 풀 |
| Session (practice) | séance | Session | セッション | 세션 |
| Plural forms | 2 (first form for 0 and 1) | 2 (first form for 1) | 1 | 1 |

All four keep the same hard rules as §8: placeholders, edge spaces and glyphs intact, product and
device names, acronyms and unit symbols untranslated, the seven language names and the spoken
calibration lines left as they are.

Source strings that no translation can make read well, worth fixing in the code:

- `DataWarningFormat` passes an English plural "s" as `%3` ("%2 hole%3"). It should be one `%n`
  plural message.
- Several `ModelBrowser` messages ("%1 already %2 %3.", "A %1 relation has no strength.") insert the
  engine token (`causes`, `leadsTo`) rather than its translated label.
- Counts built from fragments (" connected", " of ", "%1 selected") follow English word order and
  cannot agree in number or case; a whole sentence with placeholders, or a `%n` message, would.
- `PhonesPanel` tells the user to use "Pair a device" on the home screen, a label that no longer
  exists (the entry is "Add iPhone as a camera").
