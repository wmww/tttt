// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
const a = { b: { c: 1 } };
/** @type {typeof nope..b} */ let x1 = 1;
/** @type {typeof a.nope..b} */ let x2 = 1;
/** @type {typeof em..z} */ let x3 = 2;
/** @type {number} */ const sentinel = "";
// @filename: n.d.ts
declare const em: { "": { z: 1 } };
