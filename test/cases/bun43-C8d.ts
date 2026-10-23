// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
/** @typ\u0065 {number} */
export const x = true;
// @filename: b.ts
const sentinel: number = "";
