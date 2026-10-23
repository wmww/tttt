// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function isN(x: unknown): x is number;
function b(x?: { a: number }) { if (isN((x?.a))) { x.a; } }
const sentinel: number = "";
