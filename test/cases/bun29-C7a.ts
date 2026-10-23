// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "declaration": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace NS { namespace P { export interface J { j: 1 } } export declare function mk(): P.J; }
export const e = NS.mk();
const sentinel: number = "";
