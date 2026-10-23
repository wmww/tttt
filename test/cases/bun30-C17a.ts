// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f() { g.x = 1; }
namespace f { export function g() {} }
f.g.x;
const sentinel: number = "";
