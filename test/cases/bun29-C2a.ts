// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "declaration": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace O { export const v = 1; }
interface X { o: 1 }
declare function mk(): X;
export namespace I { import X = O.v; type X = 1; export const v = mk(); }
const sentinel: number = "";
