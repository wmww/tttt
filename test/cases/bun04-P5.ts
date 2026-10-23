// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function both<A, B, C, D>(f: (a: A) => B, g: (c: C) => D): (a: A, c: C) => [B, D];
declare function cst<const T>(x: T): T;
const r = both(cst, cst);
const n1: never = r;
const n2: never = r([1], [1]);
const sentinel: number = "";
