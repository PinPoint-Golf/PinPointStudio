# Procedure: render one QML component without the app

To screenshot a component without launching the app and without touching a committed file[^src]. Rendering with Qt's own `qml` tool is not an app launch ([AG-15](../controls/build.md))[^om12].

1. Copy the built module folder (`build/<tree>/qml/PinPointStudio/`) to a scratch folder and delete its `prefer :/qt/qml/PinPointStudio/` line, so the `.qml` files on disk are used.
2. For a type the app binary registers, write a `Mock.qml` (`QtObject` with the methods the component calls) and add `TypeName 254.0 Mock.qml` to the scratch `qmldir`, before `depends QtQuick`. Say in a comment that the mock is transcribed by hand: this checks the painting, not the rule.
3. Run `qml -I <scratch>/qml Harness.qml` with `QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software`; set `Theme.gradientTitles = false` in `Component.onCompleted` (the software backend can't draw the title shader, so titles would be invisible).
4. Grab with `win.contentItem.grabToImage(...)`, save to an absolute path, `Qt.quit()` on a ~600 ms timer, and read the PNG. Make the before-and-after sheet from it ([UI-17](../controls/review.md)).
5. Pair it with `qmllint` over the real files and compare the warning count before and after; only a change matters.

`appSettings is not defined` warnings from `Theme.qml` are expected.

[^src]: Practice in use since mid-2026 (the standalone QML render harness, which caught a layout cycle that compiled and passed the suite). Owner (proposed): Design Lead.
[^om12]: Product Owner, 25 Nov 2026 (#195, OM-12).
