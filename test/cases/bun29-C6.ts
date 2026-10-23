// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "declaration": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"] } }
// @filename: b.js
import { mkH } from "./m";
export function f() { return mkH(); }
/** @type {number} */ const sentinel = "";
// @filename: m.ts
interface Hidden { h: 1 }
export declare function mkH(): Hidden;
