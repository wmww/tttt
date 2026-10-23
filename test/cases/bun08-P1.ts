// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a1.ts
declare namespace N { const y: 1; }
declare namespace f { export import x = N.y; }
// @filename: a2.ts
function f() {}
f.x = 2;
f.x;
const sentinel: number = "";
