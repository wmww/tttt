// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let m: { [K in (keyof { readonly b: 1 })]: 1 };
m.b = 1;
const sentinel: number = "";
