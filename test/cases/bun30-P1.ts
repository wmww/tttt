// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.ts
const sentinel: number = "";
// @filename: f.js
const k = "foo";
export class C { static m() { this[k] = 1; } static foo() {} }
