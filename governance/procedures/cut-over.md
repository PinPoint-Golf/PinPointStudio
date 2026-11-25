# Procedure: a cut-over

For a refactor item that replaces code and deletes the old path ([AG-10](../controls/kinds-of-work.md)).

1. Before any code: the implementer lists every duty of the code to be removed (each side effect, caller, writer and test) and where each goes; the coordinator checks the list ([AG-10](../controls/kinds-of-work.md)).
2. The coordinator writes the item's deliverables as one numbered list, not a pointer to several documents ([AG-10](../controls/kinds-of-work.md)).
3. Re-point each removed test at the new owner; never delete one ([AG-10](../controls/kinds-of-work.md)).
4. Build within the item's budget ([BT-2](../controls/plan.md)), then one review and one check of its fixes; fix blocking findings only and file the rest ([AG-21](../controls/review.md), [AG-22](../controls/review.md)).
5. A defect the item's own testing finds becomes a new small item, never part of this one ([AG-22](../controls/review.md)).
