// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "allowJs": true, "checkJs": true } }
// @filename: a.js
const dec = 0;
export @dec default function f() {}
