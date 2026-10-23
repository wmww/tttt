// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k = "key";
class C14 { static [k]() {} }
namespace C14 { export function key(x: 1): void {} }
const t: boolean = C14.key;
const sentinel: number = "";
