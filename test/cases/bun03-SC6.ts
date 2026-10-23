// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.d.ts
declare module "x5" { namespace N { interface I {} } export = N; }
// @filename: b.d.ts
declare module "x5" { export interface J {} }
// @filename: f.ts
const sentinel: number = "";
