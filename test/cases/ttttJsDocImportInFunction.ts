// @allowJs: true
// @checkJs: true
// @noEmit: true
// @strict: true
// A JSDoc `@import` in a function: tsgo reparses it as an import declaration there, misplaced (TS1473 on the tag), its
// module never resolved to a file (collectModuleReferences doesn't look into functions: TS2307), its names declared in
// the function all the same. `await` in an async function and `yield` in a generator are no import names (TS1003,
// tsc's isIdentifier; ttttImportSpecifierKeywordName), with no strict-mode complaint about a JSDoc name. A misplaced import statement is checked no
// further (no TS18042)
// @filename: b.ts
export interface A { a: 1 }
// @filename: a.js
function f() {
/** @import { A } from "./b" */
/** @type {A} */
const x = 1;
}
async function g() {
/** @import { A as await } from "./b" */
}
// (`@import await from` there has no clause: the module specifier is the expression `await from "./b"`, nothing said)
async function g2() {
/** @import await from "./b" */
}
function h() {
import { A } from "./b";
}
