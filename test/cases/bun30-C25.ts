// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace N { declare module "x" { let a: number } declare module "x" { let a: number } }
const sentinel: number = "";
