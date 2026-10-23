// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true }, "files": ["a.ts"], "include": ["*.js"] }
// @filename: a.js
const y = 1;
y.foo;
// @filename: a.ts
const sentinel: number = "";
