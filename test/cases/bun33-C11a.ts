// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const v = 1;
namespace N { export { v as o }; o; }
const sentinel: number = "";
