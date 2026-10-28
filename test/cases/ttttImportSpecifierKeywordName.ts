// `await` in an async function and `yield` in a generator are no import specifier names: TS1003 like a reserved
// word's (tsc's parseImportOrExportSpecifier, isIdentifier). Known failing (symbols): tsc's baseline has no symbol for
// the imported name `A` of such a specifier, tttt's shows the export it names (bun_others.md)
// @filename: b.ts
export interface A { a: 1 }
// @filename: c.ts
function k() {
    import { A as while } from "./b";
}
async function m() {
    import { A as await } from "./b";
}
function* l() {
    import { A as yield } from "./b";
}
