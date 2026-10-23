// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUncheckedIndexedAccess": true } }
// @filename: a.ts
function g(v: [string, ...number[]]) { let b: number | undefined; [, b] = v; b.toFixed(); }
const sentinel: number = "";
