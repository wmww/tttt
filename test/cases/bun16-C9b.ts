// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace N { export function nf() {}
  if (nf) { N.nf(); } }
const sentinel: number = "";
