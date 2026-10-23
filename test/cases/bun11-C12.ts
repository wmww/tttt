// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const a: string | number;
function isStr(this: void, x: string | number) { return typeof x === "string"; }
if (isStr(a)) { a.toUpperCase(); }
const sentinel: number = "";
