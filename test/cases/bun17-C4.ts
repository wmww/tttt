// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T>(u: U, t: T): number;
const x = f({ k: 5 }, 1);
type U =
  | { k: 1 | 2 } | { k: 3 | 4 } | { z?: typeof x; k: 5 | 6 } | { k: 7 | 8 } | { k: 9 | 10 }
  | { k: 11 | 12 } | { k: 13 | 14 } | { k: 15 | 16 } | { k: 17 | 18 } | { k: 19 | 20 };
const sentinel: number = "";
