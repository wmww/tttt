// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "allowJs": true, "checkJs": true } }
// @filename: a.js
const v1 = { m() };
const v2 = { get a() };
const v3 = { async n() };
