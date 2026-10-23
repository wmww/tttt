// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "node16", "allowJs": true, "checkJs": true } }
// @filename: d.cjs
/** @import * as M from "./m.mjs" */
// @filename: m.mts
export {};
// @filename: s.ts
const sentinel: number = "";
