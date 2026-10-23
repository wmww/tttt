// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "nodenext" } }
// @filename: a.mts
import t1 from "./typed.json";
const sentinel: number = "";
// @filename: typed.d.json.ts
declare const v: { k: number };
export default v;
