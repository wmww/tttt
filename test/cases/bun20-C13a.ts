// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function assert(x: unknown): asserts x;
function p2(x: string) { if (typeof x !== "string") { assert(false); const b: null = x; } }
const sentinel: number = "";
