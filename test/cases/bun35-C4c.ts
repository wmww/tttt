// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import { v } from "v";
const r: 0 = v;
const sentinel: number = "";
// @filename: node_modules/v/no.d.ts
export const v: "no";
// @filename: node_modules/v/package.json
{ "exports": { "types@<*": "./yes.d.ts", "default": "./no.d.ts" } }
// @filename: node_modules/v/yes.d.ts
export const v: "yes";
