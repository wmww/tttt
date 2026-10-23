// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "allowJs": true, "checkJs": true } }
// @filename: a.js
class B {} class C {}
/** @augments B */
class A1 extends C extends B {}
/** @augments B */
class A2 extends C, B {}
/** @type {number} */ const sentinel = "";
