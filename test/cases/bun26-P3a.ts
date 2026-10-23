// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C {
  h = ((x) => 1) satisfies (x: this) => number;
}
new C().h(new C());
const sentinel: number = "";
