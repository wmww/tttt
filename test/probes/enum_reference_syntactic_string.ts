// @isolatedModules: true
// @filename: a.ts
export const s = "x";
// @filename: b.ts
import { s } from "./a";
export enum A { a = "lit", b = s, c = `t${s}` }
enum B { p = A.a, q = A.b, r = A.c, t = p }
enum C { u = A.b + "", v = A.a + "" }
