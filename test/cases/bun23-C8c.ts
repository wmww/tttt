// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const cs = "cstr";
declare let u: { readonly [cs]: 1 } | { readonly [cs]: 2 };
u[cs] = 1;
const sentinel: number = "";
