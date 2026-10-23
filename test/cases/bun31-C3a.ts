// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class K { isFoo(): this is Foo { return true; } }
class Foo extends K { foo = 1; }
function m(a: K) { if ((a.isFoo)()) a.foo; }
const sentinel: number = "";
