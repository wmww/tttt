// @module: es2022
// @target: es2022
// An import's local name that is a string (TS1003) is declared with a missing name: several of them conflict, and
// each is shown by its text
// @filename: a.ts
export const foo = 1;
// @filename: b.ts
import { "nope" } from "./a";
import { nope2 } from "./a";
import { "foo" } from "./a";
import { foo as "x y" } from "./a";
export { "x y" as z };
