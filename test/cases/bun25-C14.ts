// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { static Foo = 1; m() { class D implements Foo {} } }
const sentinel: number = "";
