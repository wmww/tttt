// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"],"allowJs":true,"checkJs":true } }
// @filename: a.js
async function f() {
/** @import { A as await } from "./b" */
/** @type {await} */
const x = 1;
}
/** @type {number} */
const sentinel = "";
// @filename: b.ts
export default interface D { d: 1 }
export interface A { a: 1 }
