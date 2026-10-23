// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import { prop } from "./v";
let t: prop.X;
const sentinel: number = "";
// @filename: v.ts
const v = { prop: { inner: 1 } }; export = v;
