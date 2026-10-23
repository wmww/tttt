// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { static "a-b"() { return C["a-b"]; } }
const a: 1 = C["a-b"];
const sentinel: number = "";
