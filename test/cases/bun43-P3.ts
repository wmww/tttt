// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
/** @satisfies {number} */
const /** other */ x = "";
// @filename: b.ts
const sentinel: number = "";
