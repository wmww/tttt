// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.ts
const sentinel: number = "";
// @filename: m.js
(class K { p = (K.x = 1); });
