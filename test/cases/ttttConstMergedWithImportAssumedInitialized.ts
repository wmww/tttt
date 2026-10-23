// @module: commonjs
// @noEmit: true

// A const merged with a type-only import is an alias symbol in tsc: assumed initialized (checkIdentifier's isAlias), so
// before its declaration only TS2448, not TS2454 too
// @filename: main.ts
import { X } from "./other";
console.log(X);
const X = 42;
console.log(Y);
const Y = 42;
export {};
// @filename: other.ts
export type X = {};
