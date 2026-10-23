// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.ts
const sentinel: number = "";
// @filename: f.js
export class C { [this.a = "k"]() {} }
