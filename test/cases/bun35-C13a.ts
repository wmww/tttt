// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import { v } from "j";
const r: 0 = v;
const sentinel: number = "";
// @filename: node_modules/j/index.d.ts
export const v: "index";
// @filename: node_modules/j/package.json
{ "types": './t.d.ts' }
// @filename: node_modules/j/t.d.ts
export const v: "t";
