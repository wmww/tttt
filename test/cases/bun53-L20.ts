// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "allowJs": true, "checkJs": true } }
// @filename: a.js
class B {} class C {}
/** @augments B */
class A0 extends C {}
/** @augments B */
async class A1 extends C {}
/** @augments B */
static class A2 extends C {}
/** @augments B */
export export class A3 extends C {}
/** @augments B */
accessor class A4 extends C {}
/** @extends B */
async class A5 extends C {}
/** @augments B */
export async class A6 extends C {}
/** @type {number} */ const sentinel = "";
