// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import d from "./a"; const o = { p: 1, q: d }; export default o.p;
const sentinel: number = "";
