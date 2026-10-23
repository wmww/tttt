// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o1 = { readonly m() {} };
o1.m = () => {};
const p1: number = o1;
type M1 = { [K in keyof typeof o1]: (typeof o1)[K] };
declare const m1: M1; m1.m = () => {};
const o4 = { readonly ["m"]() {} }; o4.m = null!;
const o7 = { readonly a: 1 }; o7.a = 2; const p7: string = o7;
delete o1.m;
const sentinel: number = "";
