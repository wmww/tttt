// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "esnext", "module": "esnext", "moduleResolution": "bundler", "verbatimModuleSyntax": true } }
// @filename: a.ts
import { v as Shadow } from "./m";
namespace Inner { declare const enum Shadow { A } export const y = Shadow.A; }
const sentinel: number = "";
// @filename: m.ts
export const v = 1;
