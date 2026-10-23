// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Dg = 0|1|2|3|4|5|6|7|8|9;
type K = `${Dg}${Dg}${Dg}`;
type U<T> = T extends any ? {u: T} : never;
type V<T> = T extends any ? {v: T} : never;
type A1 = U<K> & V<K>;
type A2 = U<K> & V<K>;
const sentinel: number = "";
