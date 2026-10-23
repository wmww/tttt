// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
type B<T> = { k: "a"; p: 1 } & { k: T; q: 2 };
interface X<T> extends B<T> { own: 3 }
declare const xb: X<"b">;
xb.k; xb.p; xb.q;
const lit: X<"b"> = { own: 3 };
const sentinel: number = "";
// @filename: b.ts
export {};
type V<T extends unknown[]> = [1, ...T];
interface X<T extends unknown[]> extends V<T> { }
declare const x: X<[2, 3]>;
const y: [1, 2, 3] = x;
const len: 3 = x.length;
const sentinel: number = "";
