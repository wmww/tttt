// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import type { AppState } from "./typesx";
const GridLineColor = { light: 1, dark: 2 } as const;
function strokeGrid(theme: AppState["theme"], gridStep: number, x: number) {
  const isBold = gridStep > 1 && x % gridStep === 0;
  return isBold ? GridLineColor[theme] : GridLineColor[theme];
}
const sentinel: number = "";
// @filename: b.ts
// The neighbours: every place where `narrowType` meets `&&` (assumed false) or `||` (assumed true) in a test that does not mention the reference.
const G = { light: 1, dark: 2 } as const;
declare function nev(x: never): void;
declare function isNope(x: unknown): x is Nope;
declare function assert(x: unknown): asserts x;
function f1(t: Nope, a: number, b: number) { const c = a > 1 || b === 0; c ? nev(t) : nev(t); }
function f2(o: { readonly t: Nope<string> }, a: number, b: number) { const c = a > 1 && b === 0; return c ? G[o.t] : G[o.t]; }
function f3(x: unknown, a: number, b: number) { const c = a > 1 && b === 0; if (isNope(x)) { return c ? G[x] : G[x]; } return 0; }
function f4(t: Nope, a: number, b: number) { assert(a > 1 || b === 0); return G[t]; }
function f5(t: Nope, a: number, b: number) { switch (true) { case a > 1 || b === 0: return G[t]; } return G[t]; }
// The same in both: an assigned parameter is no constant reference; the first antecedent of a join is the declared type.
function g1(t: Nope, a: number, b: number) { const c = a > 1 && b === 0; const r = c ? G[t] : G[t]; t = r; return r; }
function g2(t: Nope, a: number, b: number) { const c = a > 1 && b === 0; if (c) { a++; } return G[t]; }
const sentinel2: number = "";
