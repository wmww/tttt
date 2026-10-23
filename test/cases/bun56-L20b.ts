// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true } }
// @filename: a.js
/** @type {<>() => void} */
let a;
/** @type {<>} */
let b;
/** @type {Array<<>>} */
let c;
/** @template T */
class A { /** @type {T | undefined} */ x = undefined; }
/** @augments {A<<>} */
class E extends A {}
/** @type {number} */
const sentinel = "";
