# Gate firing across the library, 16 October 2026 (trust stage 6, f8bbbac5)

Every swing in /mnt/swingdata/Mark-Liversedge re-analysed on the studio on the reuse path (swinglab_run --write-back --session-type 1). A gate counts once per swing however many readings it touched.

```
147 swings; gatesVersion {2: 147}
sessions {'2026-07-04': 15, '2026-08-18': 11, '2026-09-09': 7, '2026-10-05': 31, '2026-10-07': 50, '2026-10-08': 32, '2026-10-13': 1}
body.shoulderLine                masked      145/147    99%
body.elbowLine                   masked      145/147    99%
body.sideBendLines               masked      143/147    97%
body.rotation                    refused      18/147    12%
body.ballPosition                refused      16/147    11%
body.lowerAddress                refused       1/147     1%
body.upperAddress                refused       1/147     1%
club.headConfidence              masked      140/147    95%
club.followThrough               masked       84/147    57%
club.lowPoint                    refused      58/147    39%
club.attackFallback              downgraded   51/147    35%
club.trackRefused                refused       7/147     5%
club.ballSuspect                 downgraded    3/147     2%
club.planeWindow                 refused       1/147     1%
club3d.illConditioned            masked       56/147    38%
club3d.planeScatter              refused      51/147    35%
clubDtl.quarantine               masked      121/147    82%
clubDtl.topOutOfView             masked       53/147    36%
clubDtl.lateEscape               masked        8/147     5%
posture.ruler                    refused      19/147    13%
posture.address                  refused       2/147     1%
posture.handLoop                 refused       1/147     1%
sequence.sightedBand             bounded     138/147    94%
sequence.clubWithheld            withheld    138/147    94%
sequence.routeWithdrawn          downgraded  120/147    82%
sequence.maxPlaceSigma           refused     100/147    68%
sequence.pairMinCorr             downgraded   60/147    41%
sequence.validityHole            bounded      17/147    12%
sequence.routePlacement          refused      10/147     7%
sequence.risingAtEnd             bounded       4/147     3%
sequence.pairRoute               downgraded    3/147     2%
sequence.reversalSpike           refused       3/147     2%
shot.captureHoles                withheld      1/147     1%
skeleton.refused                 refused       1/147     1%
```

## Addendum, 17 October 2026 (after trust stage 6 session 2, c8b3a5cd)

The library re-analysed again on the studio, same path; 147 of 147 swings clean, all at gatesVersion 2. Every
count above is unchanged. One new line:

```
sequence.noPeak                  refused       1/147     1%
```

It is 2026-10-05 swing 2, pelvis and chest peaks ("nothing to time"): a node that was unplaced with no record
before. The club track refusal (club.trackRefused, 7 swings) now covers the sequence's club peak (ks.club) on all
seven.
