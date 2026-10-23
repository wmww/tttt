// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Dg = 0|1|2|3|4|5|6|7|8|9;
type K = `${Dg}${Dg}${Dg}`;
declare function both<A, B>(a: A, b: B): A & B;
declare const x: K | boolean | { a: 1 };
declare const y: K | 1;
if (typeof x !== "object") both(x, y);
const sentinel: number = "";
