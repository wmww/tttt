// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function* g() { ({ [yield nope]() {} }); }
const sentinel: number = "";
