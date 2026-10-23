// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import { A } from "./d1";
import { B } from "./d2";
import D from "./d3";
import { C } from "./d4";
import { F, G, I } from "./d5";
import { H } from "./d6";
import { X } from "./d7";
import { Y } from "./d8";
import L, { N } from "./d9";
import { K } from "amb";
import * as d4 from "./d4";
const a: string = A;
const b: string = B;
const d: string = D;
const c: string = C;
const f: string = F, g: string = G;
let i: I;
const h: string = H;
const x: string = X;
const y: string = Y;
const l: string = L, j: string = N.J, k: string = K;
d4.C;
const sentinel: number = "";
// @filename: amb.d.ts
declare module "amb" {
  export { K } from 123;
}
// @filename: d1.ts
export { A } from 123;
export { A } from 456;
// @filename: d2.ts
export const B = 1;
export { B } from 123;
// @filename: d3.ts
export { default } from 123;
export default 1;
// @filename: d4.ts
const C = 1;
export { C } from 123;
// @filename: d5.ts
declare const name: string;
export { E as F } from name;
export * as G from `./other`;
export { type I } from (name);
// @filename: d6.ts
export * as H from 123;
export const H = 1;
// @filename: d7.ts
export { X } from "./other";
export { X } from 123;
// @filename: d8.ts
export * from "./other";
export { Y } from 123;
// @filename: d9.ts
export { default } from 123;
export namespace N {
  export { J } from 123;
}
// @filename: other.ts
export const X = 1;
export const Y = 1;
