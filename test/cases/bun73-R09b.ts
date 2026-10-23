// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "strictNullChecks": false, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f2<K extends string, X>(x: { [P in K]: X }): [K, X];
function g<T, S extends unknown, W extends { a: 1 }>(ts: T & S, tw: T & W, to: T & { b: 1 }) {
  const k1: never = f2(ts);
  const k4: never = f2(tw);
  const k5: never = f2(to);
}
const sentinel: number = "";
