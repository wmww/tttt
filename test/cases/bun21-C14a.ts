// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const assertIsString = (x: unknown): asserts x is string => {};
function f(x: unknown) { assertIsString(x), 0; x.length; }
const sentinel: number = "";
