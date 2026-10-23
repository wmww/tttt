// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k = "foo";
class D3 { foo = 2; [k] = 3 }
const sentinel: number = "";
