// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function assert(x: unknown): asserts x;
declare function fail(): never;
function t7b(x: string | number | boolean) { fail(); assert(typeof x === "string" || typeof x === "number"); const a: null = x; }
const sentinel: number = "";
