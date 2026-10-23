// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022" } }
// @filename: a.ts
function f1<T extends object>(t: T) {
  const r = { ...t, get a() { return "g"; } };
  r.a = "x";
  r.zzz;
}
class K<T extends object> {
  constructor(public t: T) {}
  m() {
    const s = { ...this, b: 1, get c() { return 1; } };
    s.c = 2;
  }
}
const sentinel: number = "";
