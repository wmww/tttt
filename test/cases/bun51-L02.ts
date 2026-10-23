// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
namespace N { module "x" {} }
function f() { module "y" {} }
export { N, f };
export const sentinel: number = "";
