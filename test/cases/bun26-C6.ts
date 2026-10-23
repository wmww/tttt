// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface P { a: number }
namespace P { export const z = 1; }
const i: { [k: string]: number } = null! as P;
const sentinel: number = "";
