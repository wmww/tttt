// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["b.ts", "a.ts"] }
// @filename: a.ts
function f() {}
f.x = "";
const sentinel: number = "";
// @filename: b.ts
declare namespace Y { let v: string }
declare namespace f { export import x = Y.v; }
