// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: { p?(v: unknown): v is string } | undefined;
function f(v: unknown) { if (o?.p!(v)) v.length; }
const sentinel: number = "";
