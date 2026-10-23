// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function assert(x: unknown): asserts x;
function t10(x: string) { assert(false || false); const a: null = x; }
const sentinel: number = "";
