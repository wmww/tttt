// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const l: { readonly s: unique symbol };
({ a: 1 })[l.s];
const sentinel: number = "";
