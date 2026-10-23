// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C {
  r!: { a: 1, b: 2 } | { a: 2, c: 3 };
  m(q: this["r"]) {
    const { a, ...rest } = q;
    const x: { b: 2 } | { c: 3 } = rest;
  }
}
const sentinel: number = "";
