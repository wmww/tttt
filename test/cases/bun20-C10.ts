// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "strictNullChecks": false, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function isEmpty(x: unknown): x is {};
function f(x: unknown) { if (!isEmpty(x)) { const b: string = x; } }
const sentinel: number = "";
