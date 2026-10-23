// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace N { export interface Y {} }
class C implements (N).Y {}
const sentinel: number = "";
