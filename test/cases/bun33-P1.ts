// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts", "b.ts"] }
// @filename: a.ts
function f() {}
f.x = 1;
// @filename: b.ts
declare namespace Y { interface T {} }
declare namespace f { export import x = Y.T; }
import z = f.x;
z;
const sentinel: number = "";
