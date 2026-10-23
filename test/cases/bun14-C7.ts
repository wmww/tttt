// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: { readonly ro: number; rw: number };
declare const k1: "aa" | "ro";
o[k1] = 1;
const sentinel: number = "";
