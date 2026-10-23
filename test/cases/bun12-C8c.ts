// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noUncheckedIndexedAccess": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
let full: string | undefined;
[full] = /a/.exec("a")!;
full.length;
const sentinel: number = "";
