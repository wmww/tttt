// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import { OnlyT } from "./real";
OnlyT;
const sentinel: number = "";
// @filename: real.ts
namespace g2 { export interface OnlyT { t: 1 } }
const g2 = { v: 1 };
export = g2;
