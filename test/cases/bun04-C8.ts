// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function both2<A, B, C, D>(f: new (a: A) => B, g: new (c: C) => D): (a: A, c: C) => [B, D];
class Box<T> { constructor(public v: T) {} }
class Bag<T> { constructor(public w: T) {} }
class Two<T, U> { constructor(public w: [T, U]) {} }
const n: never = both2(Box, Bag);
const r = both2(Two, Two);
const sentinel: number = "";
