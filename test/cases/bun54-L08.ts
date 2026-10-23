// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
import { A1, A2, A3, A4, A5, A6, A7, A8 } from "./p";
export type Range = [
  /** The start, see {@link A1} */
  start: number,
  /** {@link A2} */
  end?: number,
  /** {@link A3} */
  ...rest: string[],
];
export function f(t: [
  /** {@link A4} */
  a: string,
]) { return t; }
export type Unnamed = [
  /** {@link A5} */
  string,
];
export type Same = [ /** {@link A6} */ a: string ];
export type Nested = { p: readonly [
  /** @see {@link A7} */
  a: string,
] };
export type Generic<T> = [
  /** {@link T} {@link A8} */
  a: T,
];
export const sentinel: number = "";
// @filename: p.ts
export class A1 {} export class A2 {} export class A3 {} export class A4 {} export class A5 {} export class A6 {} export class A7 {} export class A8 {} export class A9 {}
export class B1 {} export class B2 {} export class B3 {} export class B4 {} export class B5 {} export class B6 {} export class B7 {} export class B8 {} export class B9 {}
