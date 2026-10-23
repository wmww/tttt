// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function isNum(x: unknown): x is number;
function n(a?: { b: unknown }) { if (isNum((a?.b))) a.b; }
const sentinel: number = "";
