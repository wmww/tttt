// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import { x } from "./a"; const v = { x: 1, y: x }; export = v;
const sentinel: number = "";
