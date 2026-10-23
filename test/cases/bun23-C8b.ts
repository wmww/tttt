// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const cs = "cstr";
let o = { [cs]: { n: 1 } }; o = {};
const sentinel: number = "";
