// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const a1 = { abstract m() {} };
const a2 = { readonly m() {} };
const a3 = { accessor m() {} };
const a4 = { public public m() {} };
const a5 = { public static m() {} };
const a6 = { static public m() {} };
const sentinel: number = "";
