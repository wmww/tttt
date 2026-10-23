// @module: esnext
// @target: esnext
// @noEmit: true

// A source phase import's attributes are checked like any import's (TS2858), though its module isn't resolved
// @filename: a.ts
export {};
// @filename: b.ts
import source a from "./a" with { type: 42 };
import b from "./a" with { type: 42 };
