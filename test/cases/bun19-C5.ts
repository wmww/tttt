// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I { a: number }
namespace I { export const x = 1 }
declare let i: I;
const r: { [k: string]: number } = i;
const sentinel: number = "";
