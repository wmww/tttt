// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import { Sub } from "graphics";
let s1: Sub.S;
const sentinel: number = "";
// @filename: amb.d.ts
declare namespace graphics { namespace Sub { interface S { s: 1 } } }
declare var graphics: { Sub: string };
declare module "graphics" { export = graphics; }
