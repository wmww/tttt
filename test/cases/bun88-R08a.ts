// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "module": "esnext", "target": "es2022" } }
// @filename: a.js
/** @type {typeof globalThis.} */ var v8 = "x";
/** @type {number} */ const sentinel = "";
