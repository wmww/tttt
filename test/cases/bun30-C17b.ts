// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { p = (g.x = 1) }
namespace C { export function g() {} }
C.g.x;
const sentinel: number = "";
