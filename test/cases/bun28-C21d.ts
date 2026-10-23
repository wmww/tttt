// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022"}}
// @filename: a.ts
import type { type a } from "./zz";
const sentinel: number = "";
// @filename: zz.ts
export {};
