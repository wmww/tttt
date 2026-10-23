// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const loc = 1;
namespace Inner { export { loc }; }
export {};
const sentinel: number = "";
