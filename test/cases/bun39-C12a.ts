// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es5"] } }
// @filename: a.ts
let x;
// @filename: c.ts
function f() { return x; }
const sentinel: number = "";
