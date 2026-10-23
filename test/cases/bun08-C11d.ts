// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import * as ns from "./a"; const v = { n: 1, m: ns }; export = v;
const sentinel: number = "";
