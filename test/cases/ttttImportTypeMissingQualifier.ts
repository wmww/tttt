// @allowJs: true
// @checkJs: true
// @noEmit: true
// An import type whose qualifier is a missing name (`import("./m").` then nothing) has no qualifier to tsc's
// getTypeFromImportTypeNode (NodeIsMissing): TS1340 for a module that is no type, not TS2694 for a member named ''
// (bun 33-P9a)
// @filename: m.ts
export {};
// @filename: a.js
/** @type {import("./m").} */ let b1 = 1;
// @filename: b.ts
let a: import("./m").;
let b: typeof import("./m").;
