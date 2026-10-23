// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
/** @import a-b from "./p" */
// @filename: b.ts
const sentinel: number = "";
// @filename: p.ts
export interface Foo {}
