// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "declaration": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace N { interface I { readonly p: unique symbol } export declare const x: I["p"]; }
export const a = [N.x] as const;
const sentinel: number = "";
