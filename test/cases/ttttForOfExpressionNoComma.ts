// @noEmit: true
// A for-of statement's expression is an assignment expression, a for-in's may be a comma expression (tsc's
// parseForOrForInOrForOfStatement): `for (x of a, b)` is ')' expected at the `,`, the rest a statement (mutation seed
// 20261106, controlFlowNoCircularity: `[{ -1], intermediate: ...]` once a property was dropped)
declare var a: any, b: any;
for (const p of a, b) {}
for (const p in a, b) {}
for (const p of [{ -1], intermediate: [-1, 0] }]) {}
