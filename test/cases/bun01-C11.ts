// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
namespace N { const a = 1; export { a as b }; b; }
const sentinel: number = "";
