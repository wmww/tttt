// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "allowJs": true, "checkJs": true } }
// @filename: a.js
@dec declare module "zz" {}
@dec declare module "yy";
function dec() {}
/** @type {number} */ const sentinel = "";
// @filename: b.js
@dec export as namespace X;
// @filename: c.js
@dec declare global {}
export {};
