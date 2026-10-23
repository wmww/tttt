// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es5"] } }
// @filename: a.ts
// @ts-expect-error
let x;
// @filename: c.ts
function f() { return x; }
const sentinel: number = "";
