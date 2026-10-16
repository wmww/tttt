# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-16 (triage session, todo entry 10 of the sixteenth window): the unique symbol name in
TS2540 was fixed (`ttttReadonlyUniqueSymbolName`), TS5088 through `typeof this` (root cause found) and the
content-mapped outputs went to bugs/dts_emit.md, the human's stop request was honoured (`claude-stop`).

- **The 12-core bench is in** (2026-10-16, measurements.md "The 12-core machine"): past K12 checkers buy 0-11% wall
  for ~170 MB each on vscode (6.5 GB at the default K24); `--checker-memory 64` at K24 is -1.75 GB for +2.4%. Plan 75
  question 1 has its numbers (a K cap and/or a total budget); plan 72's declare+link is 5% on vscode, 15% on webpack.
