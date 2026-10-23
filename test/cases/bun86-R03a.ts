// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.d.ts", "b.ts"] }
// @filename: a.d.ts
declare namespace f { interface x { j: 1 } }
// @filename: b.ts
function f() {}
f.x = "s";
const p: number = f.x;
const sentinel: number = "";
