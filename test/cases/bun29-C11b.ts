// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type T = { readonly t: unique symbol };
declare const o: T;
({ a: 1 })[o.t];
const sentinel: number = "";
