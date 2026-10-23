// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "types": [], "lib": ["es2022"] } }
// @filename: b.js
/** @typedef {{ q: string= }} T1 */
/** @typedef {{ q: string=}} T2 */
/** @typedef {{ q: string=    }} T3 */
/** @type {{ q: string= , r: number }} */
let v4;
/** @typedef {{ q: string= 1 }} T5 */
/** @type {number} */
const sentinel = "";
