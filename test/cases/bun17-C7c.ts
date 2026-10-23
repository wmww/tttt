// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace C { export enum NaNy { N = 0 / 0 } }
namespace D { export enum NaNy { N = 0 / 0 } }
declare let cn: C.NaNy, dn: D.NaNy;
cn = dn;
const sentinel: number = "";
