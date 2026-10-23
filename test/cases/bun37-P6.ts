// @filename: tsconfig.json
{ "compilerOptions": { "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true }, "include": ["*.mİn.*", "a.ts"] }
// @filename: a.mİn.min.js
const y = 1;
y.foo;
// @filename: a.ts
const sentinel: number = "";
