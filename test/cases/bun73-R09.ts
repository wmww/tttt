// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function keys<K extends string>(o: Record<K, unknown>): K[];
declare function f2<K extends string, X>(x: { [P in K]: X }): [K, X];
function strict<R extends {}, W extends { a: 1 }, V extends { b: 2 }>(rw: R & W, wv: W & V, wo: W & { c: 3 }) {
  const k1: "a"[] = keys(rw);
  const k2: ("a" | "b")[] = keys(wv);
  const k3: ("a" | "c")[] = keys(wo);
  const p1: never = f2(rw);
  const p2: never = f2(wv);
  const p3: never = f2(wo);
}
const sentinel: number = "";
