# Procedure: a cut-over

For a change that replaces code and deletes the old path ([AG-10](../controls/kinds-of-work.md)).

1. Before any code: list every duty of the code to be removed (each side effect, caller, writer and test) and where each goes.
2. Write the change's deliverables as one numbered list, not a pointer to several documents.
3. Re-point each removed test at the new owner; never delete one.
4. Build with targeted test builds and one app build at the end ([BT-2](../controls/plan.md)). A cut-over that migrates stored data, or deletes files, gets one independent review for real bugs and data-loss risks ([AG-21](../controls/review.md)).
5. A defect found along the way is fixed if it is in scope; otherwise it is an issue only if it is a real bug or a real data-loss risk ([AG-22](../controls/review.md)).
