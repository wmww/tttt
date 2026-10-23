// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
import { i1 } from "./b";
export function k1(
  /** {@link v} */
  a: number) { let v = 1; return a }
export function k2(
  /** {@link L} */
  a: number) { type L = 1; return a }
export function k3(
  /** {@link i1} */
  a: number) { let i1 = 1; return a + i1 }
export const sentinel: number = "";
// @filename: b.ts
export const i1 = 1;
