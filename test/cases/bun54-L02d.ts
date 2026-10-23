// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "nodenext" } }
// @filename: a.mts
import t1 from "z";
import { default as t2 } from "z";
t1.amb; t2.amb;
const sentinel: number = "";
// @filename: amb.d.json.ts
declare module "z" { const v: { amb: number }; export default v; }
