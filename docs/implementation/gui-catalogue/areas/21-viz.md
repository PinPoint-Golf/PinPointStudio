# 3-D body/IMU views

*Directory:* `src/Gui/viz/`  
*4 components · 1,600 lines of QML*

QtQuick3D views of IMUs, the arm and the body, plus the View3D keep-alive guard.

**C++ in this area:** `BodyPoseAdapter`, `LiveWristAngles`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [ArmVizView](#armvizview) | 495 | `Item` | 2 | 1 | 1 local comp. |
| [BodyVizView](#bodyvizview) | 880 | `Item` | 2 | 0 | 2 local comp. |
| [ImuVizView](#imuvizview) | 181 | `View3D` | 2 | 0 | 1 local comp. |
| [View3DKeepAlive](#view3dkeepalive) | 44 | `View3D` | 2 | 0 |  |

### ArmVizView
`src/Gui/viz/ArmVizView.qml` · 495 lines · root `Item` · 1 hard-coded colour(s)

3-D rigid Y-bot arm (upper arm, forearm, hand as bone-local GLBs) driven by IMU quaternions, with orbit camera (drag/scroll) and orientation tabs.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `readonly bool rightHanded`, `readonly vector3d armCenter`, `readonly real camDistance`, `readonly QtObject imuLeadForearm`, `readonly QtObject imuLeadHand`, `readonly QtObject imuLeadUpperArm`, `readonly bool upperArmKnown`, `readonly quaternion hmReferenceQuat`, `readonly bool leadForearmIsHm`, `readonly bool leadHandIsHm`, `readonly bool leadUpperArmIsHm`, `readonly quaternion worldToScene`, `readonly quaternion leftRestOffset`, `readonly quaternion rightRestOffset` … (+6 more)
- **Functions:** `quatMul(a, b)`, `quatInv(q)`, `roleIsHackMotion(role)`, `quatApplyCalib(imuInst, raw, isHm)`
- **Local components:** `OrientationTab`
- **Uses:** `ImuMounts`
- **Used by (2):** `CapturePage`, `CheckArmPage`
- **Exercised by tests/tools:** `SetupDriver`, `tst_setup_check`, `tst_setup_guide_render`, `tst_setup_rolesviz`, `tst_setup_smoke`, `view3d_unload`

### BodyVizView
`src/Gui/viz/BodyVizView.qml` · 880 lines · root `Item` · 4 hard-coded colour(s)

3-D full Y-bot body assembled from bone-local GLBs and driven by a pose source (optionally mirrored), with joint markers, lead-arm highlight, loading progress and orbit camera.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `QtObject poseSource`, `bool mirroredSource`, `bool showJoints`, `bool rightHanded`, `bool highlightLeadArm`, `color leadArmColor`, `bool showOrientationTabs`, `bool useLeadArmOverride`, `quaternion leadArmOverrideRotation`, `quaternion leadForeArmOverrideRotation`, `bool useTrailArmOverride`, `quaternion trailArmOverrideRotation`, `quaternion trailForeArmOverrideRotation`, `readonly real hmCalRaiseDeg` … (+12 more)
- **Signals:** `leadArmAnimFinished()`
- **Functions:** `resetArmAnimation(fromQ, foreFromQ)`
- **Local components:** `OrientationTab`, `JointMarker`
- **Used by (2):** `CapturePage`, `ImuCalibrationFlow`
- **Exercised by tests/tools:** `CalibDriver`, `tst_setup_smoke`, `view3d_unload`

### ImuVizView
`src/Gui/viz/ImuVizView.qml` · 181 lines · root `View3D` · 1 hard-coded colour(s)

3-D View3D of a single IMU as a labelled cube (Top/Bottom/Front/Back/Left/Right faces) driven by the fused quaternion of its controller, with the Z-up IMU world to Y-up scene basis change applied on a parent node.  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `QtObject controller`
- **Local components:** `FaceTex`
- **Used by (2):** `CapturePage`, `ImusPanel`

### View3DKeepAlive
`src/Gui/viz/View3DKeepAlive.qml` · 44 lines · root `View3D`

> A View3D that is never shown, held for the window's lifetime, so destroying a page's 3-D view
> never destroys the LAST View3D in that window.
> ⚠ Qt 6.11.0's debug Quick 3D frees the window's 3-D renderer state when its last View3D goes,
> while the render thread is still using it: on Windows/D3D11 the next sync writes into freed
> memory (0xfeeefeee… in Qt6Quick3DRuntimeRenderd) and the process dies — every time, with a
> plain View3D { camera, light, cube } unloaded from a Loader (2026-10-08). Qt's own runner on the
> RELEASE libraries survives the same file; one more View3D anywhere in the window, even this
> invisible one, makes the debug libraries survive it too. The release libraries may carry the
> same use-after-free silently, so the app holds one of these per top-level window rather than
> relying on some other screen's 3-D view staying alive.
> It costs nothing: an invisible item is never rendered. Put one in every top-level window that
> can host a View3D (Main.qml, PpSessionDiagnosticsWindow.qml), and in test windows that load and
> unload 3-D views (src/Gui/tests/setup/support). src/Gui/tests/probes/view3d_unload.qml and
> the session_setup_ui suite are the checks.

- **Used by (2):** `Main`, `PpSessionDiagnosticsWindow`
- **Exercised by tests/tools:** `CalibDriver`, `SetupDriver`, `tst_setup_mounts`, `tst_setup_rolesviz`, `tst_setup_smoke`
