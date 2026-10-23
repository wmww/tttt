// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<A, B>() { const p: 1 = null! as { [K in (keyof (A | B))]: K }; }
const sentinel: number = "";
