// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class K { foo() {}
  m() { if (this.foo) { (function (this: K) { this.foo(); }); } }
}
const sentinel: number = "";
