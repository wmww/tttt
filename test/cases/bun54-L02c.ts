// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "nodenext", "paths": { "x.json": ["./typed.json"] } } }
// @filename: a.mts
import t1 from "x.json";
t1.amb; t1.k;
const sentinel: number = "";
// @filename: amb.d.ts
declare module "x.json" { const v: { amb: number }; export default v; }
// @filename: typed.d.json.ts
declare const v: { k: number };
export default v;
