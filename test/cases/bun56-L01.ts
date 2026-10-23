// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
/** @template T */
class A { /** @type {T | undefined} */ x = undefined; }
/** @augments {A<=} */
class B extends A {}
/** @augments {A<<=} */
class C extends A {}
/** @augments A<= */
class D extends A {}
/** @implements {A<=} */
class E { x = undefined }
/** @type {number} */
const sentinel = "";
