// export * cycles, own exports shadowing star exports, the same name from two stars being ambiguous (dropped silently)
// @filename: a.ts
export * from "./b";
export const fromA = 1;
export const shadowed = "a";
// @filename: b.ts
export * from "./a";
export * from "./c";
export const fromB = 2;
export const shadowed = 2;
export const twice = 1;
// @filename: c.ts
export const twice = "c";
export const fromC = 3;
// @filename: main.ts
import { fromA, fromB, fromC, shadowed } from "./a";
import * as ns from "./a";
const s: string = shadowed;
const n: number = fromA + fromB + fromC + ns.fromC;
ns.twice;
