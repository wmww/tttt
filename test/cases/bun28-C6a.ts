// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022"}}
// @filename: a.ts
declare module "m" { export type a = 1; }
declare module "n" { import type { type a } from "m"; }
const sentinel: number = "";
