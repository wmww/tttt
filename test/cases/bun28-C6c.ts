// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "allowJs": true, "checkJs": true}}
// @filename: a.ts
export type A = 1;
const sentinel: number = "";
// @filename: b.js
/** @import { type A } from "./a" */
/** @type {A} */
const q = 2;
