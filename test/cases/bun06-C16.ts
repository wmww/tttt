// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace NI { export interface Inner<T> { } export const v = 1; }
const x1 = NI.Inner<string>;
const sentinel: number = "";
