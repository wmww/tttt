// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k = "key";
class C3 { static [k] = 1 }
namespace C3 { export const key = "s"; }
const t: boolean = C3.key;
const sentinel: number = "";
