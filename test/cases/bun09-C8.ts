// @filename: tsconfig.json
{ "compilerOptions": { "strict": false, "noImplicitAny": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f4([g = null] = []) {}
f4([]);
const sentinel: number = "";
