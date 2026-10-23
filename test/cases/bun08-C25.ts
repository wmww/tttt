// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace N { export const y = 1; }
declare const k: "x";
function f() {}
namespace f { export import x = N.y; }
f[k] = 2;
const sentinel: number = "";
