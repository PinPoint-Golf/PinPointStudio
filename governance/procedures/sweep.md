# Procedure: a sweep over many swings

For any run over many swings: a sweep, a re-analysis pass, a bisect of three or more swings ([AD-23](../controls/kinds-of-work.md)).

1. Test the hypothesis on the one swing that shows the problem first ([AD-22](../controls/kinds-of-work.md)).
2. Check the changed rule over every swing, corpus and library, read-only; give a time estimate; say whether the sweep may need repeating ([AD-24](../controls/kinds-of-work.md)).
3. Back up to the shared scratch area, never a local build folder ([AD-26](../controls/kinds-of-work.md)).
4. Build the release on the primary platform through the project's committed script for that machine; uncommitted work goes as a patch, never a commit ([AD-23](../controls/kinds-of-work.md), [GIT-3](../controls/commit.md)). The machine and how it is driven are in the local agent file.
5. Write run outputs to the shared scratch area ([AD-27](../controls/kinds-of-work.md)); read the first swing's reuse line before letting the run go on ([AD-24](../controls/kinds-of-work.md)).
6. After a change to diagnostics content, regrade the ledgers once re-analysis is done ([AD-25](../controls/kinds-of-work.md)).
7. Judge the pass by the per-swing diff against the backup ([AD-24](../controls/kinds-of-work.md)); measure the pose noise floor if pose ran ([AD-20](../controls/kinds-of-work.md)).
8. Copy the derived CSVs and gate tables into the repo, then delete the run trees and say how much ([AD-27](../controls/kinds-of-work.md)); delete the backup once the owner accepts the result ([AD-26](../controls/kinds-of-work.md)).
