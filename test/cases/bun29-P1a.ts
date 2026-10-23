// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "declaration": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type T = { readonly s: unique symbol };
declare const o: T;
export const v = { [o.s]: 1 };
const sentinel: number = "";
