// @filename: tsconfig.json
{ "compilerOptions": { "strict": false, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true }, "files": ["a1.js", "a2.ts"] }
// @filename: a1.js
var lit = {};
/** @this {typeof lit} */ function g() { return (this).missing; }
var v = g();
// @filename: a2.ts
var v: string;
const sentinel: number = "";
