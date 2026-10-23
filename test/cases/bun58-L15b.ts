// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "module": "esnext", "moduleResolution": "bundler" }, "files": ["y.JS", "m.MJS", "a.ts"] }
// @filename: a.ts
const sentinel: number = "";
// @filename: m.MJS
/** @type {number} */
export const mm = "";
// @filename: y.JS
/** @type {number} */
const yy = "";
function f(a) { return a; }
