// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type A1 = { [K in "a"]: A1["a"] };
const sentinel: number = "";
