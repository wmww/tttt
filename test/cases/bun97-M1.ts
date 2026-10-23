// @filename: tsconfig.json
{
 "compilerOptions": {
  "strict": true,
  "noEmit": true,
  "target": "esnext",
  "module": "esnext",
  "moduleResolution": "bundler",
  "lib": [
   "esnext"
  ],
  "types": [],
  "skipLibCheck": true,
  "noUnusedLocals": true
 }
}
// @filename: a.ts
import { I } from "./lib";
export const v = 1 & I;
// @filename: b.ts
import { A } from "./lib";
export const v = A;
// @filename: c.ts
import { N } from "./lib";
export const v = N;
// @filename: d.ts
interface J { x: number }
export const v = J;
// @filename: f.ts
import type { K } from "./lib";
export const v = new K();
// @filename: g.ts
import { I } from "./lib";
export const v = I;
export const w: I = { x: 1 };
// @filename: h.ts
import { I, A } from "./lib";
export const v = I.x;
export const w = A();
// @filename: j.ts
import { I } from "./lib";
export const v = { I };
// @filename: k.ts
import { I } from "./lib";
export { I };
// @filename: l.ts
import { I } from "./lib";
export const v = ({}) instanceof I;
// @filename: lib.ts
export interface I { x: number }
export type A = { y: number };
export declare namespace N { interface T {} }
export class K {}
// @filename: z.ts
export const sentinel: number = "";
