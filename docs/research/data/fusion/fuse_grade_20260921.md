# Shaft fusion — graded against its fusion-off control, 2026-09-21

Run: `build/fusion_run/run24.sh <root>` (07-04 s1–15 taped 7-iron, 06-11 s1–9 bare wedge; pinned face-on and DTL poses), control = the same with `--params {"shaft.fusion.enabled": false}`.
Table: `python3 tools/shaftlab/fuse_grade.py <run> <control>`. Cameras idealised (yaw 0, pitch 0), uncalibrated.

| swing | fused/pub | bridged | sign | offPl | back incl/rms | down incl | heading | rms | n | offered | ellipse k@node | fused k@node | club peak °/s ctl→fused | club t ms ctl→fused | route |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2026-06-11 s0001 | 103/198 | 10 | 0 | 1 | 63.0/6.0 | 63.2 | -92.3 | 3.35 | 15 | yes | 0.830@-49 | 0.892@175 | 2172→1818 | 3441→3441 | faceOn+dtl |
| 2026-06-11 s0002 | 105/181 | 4 | 0 | 0 | 57.7/7.3 | 60.2 | -93.6 | 2.68 | 19 | yes | 0.854@-57 | 0.866@174 | 1833→1808 | 3447→3414 | faceOn+dtl |
| 2026-06-11 s0003 | 98/188 | 3 | 0 | 0 | 65.0/11.6 | 63.9 | -110.0 | 4.77 | 14 | yes | 0.737@-1 | 0.844@145 | 1291→987 | 3381→3381 | faceOn+dtl |
| 2026-06-11 s0004 | 101/179 | 7 | 0 | 0 | 67.9/7.1 | 63.4 | -97.5 | 2.64 | 14 | yes | 0.900@8 | 0.887@165 | 1604→1637 | 3397→3405 | faceOn+dtl |
| 2026-06-11 s0005 | 88/176 | 6 | 0 | 0 | 62.4/6.2 | 64.7 | -91.6 | 1.85 | 14 | yes | 0.904@10 | 0.904@177 | 1622→1586 | 3418→3422 | faceOn+dtl |
| 2026-06-11 s0006 | 89/171 | 0 | 0 | 0 | 62.2/3.0 | 63.8 | -94.5 | 2.03 | 17 | yes | 0.903@42 | 0.894@171 | 1745→1664 | 3452→3465 | faceOn+dtl |
| 2026-06-11 s0007 | 85/173 | 7 | 0 | 0 | 62.7/3.3 | 63.1 | -91.0 | 3.08 | 14 | yes | 0.735@-17 | 0.891@178 | 1911→1661 | 3490→3465 | faceOn+dtl |
| 2026-06-11 s0008 | 107/197 | 7 | 0 | 0 | 62.9/3.1 | 59.8 | -89.8 | 1.93 | 18 | yes | 0.933@22 | 0.864@0 | 1557→1618 | 3439→3451 | faceOn+dtl |
| 2026-06-11 s0009 | 83/170 | 14 | 0 | 1 | 61.7/5.9 | 62.8 | -97.0 | 1.32 | 10 | yes | 0.646@63 | 0.883@167 | 1776→1240 | 2773→2794 | faceOn+dtl |
| 2026-07-04 s0001 | 108/188 | 15 | 0 | 0 | 54.4/22.7 ⚠ | 63.8 | -95.3 | 3.30 | 16 | yes | 0.907@23 | 0.893@169 | 1774→1696 | 3422→3431 | faceOn+dtl |
| 2026-07-04 s0002 | 77/168 | 18 | 0 | 2 | 85.3/20.5 ⚠ | 65.1 | -96.0 | 1.94 | 12 | yes | 0.890@25 | 0.902@167 | 1899→1794 | 3432→3445 | faceOn+dtl |
| 2026-07-04 s0003 | 97/176 | 18 | 0 | 0 | 87.7/17.1 ⚠ | 62.3 | -95.9 | 3.18 | 18 | yes | 0.937@-20 | 0.881@169 | 1616→1671 | 3445→3453 | faceOn+dtl |
| 2026-07-04 s0004 | 113/196 | 19 | 0 | 0 | 59.0/6.2 | 60.5 | -90.5 | 1.23 | 15 | yes | 0.903@40 | 0.870@179 | 1930→1851 | 3426→3430 | faceOn+dtl |
| 2026-07-04 s0005 | 104/187 | 26 | 0 | 0 | 50.8/9.3 | 60.7 | -89.4 | 1.09 | 15 | yes | 0.919@35 | 0.872@1 | 1835→1779 | 3420→3428 | faceOn+dtl |
| 2026-07-04 s0006 | 93/175 | 7 | 0 | 0 | 60.8/5.4 | 60.0 | -88.2 | 0.76 | 17 | yes | 0.896@27 | 0.866@3 | 1967→1983 | 3438→3447 | faceOn+dtl |
| 2026-07-04 s0007 | 100/187 | 21 | 0 | 1 | 61.9/5.5 | 60.6 | -86.2 | 2.83 | 12 | yes | 0.929@-5 | 0.870@7 | 1836→1867 | 3476→3472 | faceOn+dtl |
| 2026-07-04 s0008 | 102/185 | 21 | 0 | 0 | 51.5/10.9 | 61.3 | -88.6 | 1.17 | 15 | yes | 0.949@11 | 0.877@3 | 1612→1690 | 3444→3452 | faceOn+dtl |
| 2026-07-04 s0009 | 87/177 | 6 | 0 | 0 | 50.0/9.3 | 60.5 | -87.6 | 1.28 | 15 | yes | 0.886@31 | 0.869@4 | 1925→1974 | 3433→3445 | faceOn+dtl |
| 2026-07-04 s0010 | 119/202 | 23 | 0 | 0 | 62.4/6.7 | 59.5 | -87.3 | 0.91 | 17 | yes | 0.909@38 | 0.861@5 | 1682→1717 | 3443→3455 | faceOn+dtl |
| 2026-07-04 s0011 | 87/182 | 18 | 0 | 0 | 60.8/6.1 | 60.3 | -89.1 | 1.02 | 15 | yes | 0.930@26 | 0.869@2 | 1839→1839 | 3425→3433 | faceOn+dtl |
| 2026-07-04 s0012 | 76/165 | 7 | 0 | 0 | 60.6/5.4 | 59.3 | -88.5 | 1.22 | 13 | yes | 0.979@-14 | 0.860@3 | 1867→2043 | 3449→3449 | faceOn+dtl |
| 2026-07-04 s0013 | 97/172 | 14 | 0 | 0 | 60.4/4.9 | 61.4 | -87.6 | 0.60 | 13 | yes | 0.940@13 | 0.877@4 | 1864→1922 | 3445→3449 | faceOn+dtl |
| 2026-07-04 s0014 | 95/184 | 3 | 7 | 2 | 61.2/5.9 | 59.2 | -90.1 | 0.88 | 17 | yes | 0.829@-37 | 0.859@180 | 1594→1824 | 3494→3435 | faceOn+dtl |
| 2026-07-04 s0015 | 103/182 | 15 | 0 | 0 | 54.5/8.2 | 60.2 | -93.2 | 1.10 | 14 | yes | 0.893@27 | 0.866@174 | 2021→2036 | 3447→3460 | faceOn+dtl |

PARITY — what differs from the control besides `timings`:
  20 swings: analysis keys ['club3d (only run)', 'kinematicSequence.gainsDps', 'kinematicSequence.gapsMs', 'kinematicSequence.nodes', 'versions.shaftFusion (only run)']
   2 swings: analysis keys ['club3d (only run)', 'kinematicSequence.nodes', 'versions.shaftFusion (only run)']
   1 swings: analysis keys ['club3d (only run)', 'kinematicSequence.gainsDps', 'kinematicSequence.gapsMs', 'kinematicSequence.nodes', 'kinematicSequence.order', 'kinematicSequence.orderResolved', 'kinematicSequence.verdict', 'versions.shaftFusion (only run)']
   1 swings: analysis keys ['club3d (only run)', 'kinematicSequence.gainsDps', 'kinematicSequence.nodes', 'versions.shaftFusion (only run)']
  24 swings: metric series ['clubAngularSpeed']
