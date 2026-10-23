// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noUncheckedIndexedAccess": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const r: [string, ...number[]];
let f: string | number, g: number | boolean | undefined;
[f, g] = r;
if (typeof g !== "number") { const u: boolean = g; }
const sentinel: number = "";
