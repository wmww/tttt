// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const v: { readonly s: unique symbol };
declare const k: typeof v.s;
const o: 1 = { [k]: 1 };
const sentinel: number = "";
