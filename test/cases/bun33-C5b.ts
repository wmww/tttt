// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace M { export interface I {} }
class X implements (M).I {}
const sentinel: number = "";
