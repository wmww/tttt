// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f() {}
f.a = "b" as const;
f[k] = 1;
declare const k: (typeof f)["a"];
f.b;
const sentinel: number = "";
