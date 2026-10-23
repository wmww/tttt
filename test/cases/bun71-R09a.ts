// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "noUnusedLocals": true, "noUnusedParameters": true } }
// @filename: a.ts
import { I } from "./m";
import { T } from "./m";
import { NT } from "./m";
import D from "./m";
import * as ns from "./m";
import M = ns.NT;
export const t1 = I;
export const t2 = T;
export const t3 = NT;
export const t4 = D;
export const t5 = M;
const sentinel: number = "";
// @filename: m.ts
export interface I { i: number }
export type T = number;
export namespace NT { export type X = 1 }
export default interface D {}
export class C {}
