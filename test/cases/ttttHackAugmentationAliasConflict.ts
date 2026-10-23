// @strict: true
// A module augmentation's `export { x as y }` meeting an existing export `y` merges with it (TS2323, TS2484);
// hacks.md `merge_symbol()`: an alias in an augmentation isn't merged
// @filename: a.ts
export const y = 1;
export const x = "s";
// @filename: b.ts
import { y } from "./a";
declare module "./a" { export { x as y } }
const z: number = y;
