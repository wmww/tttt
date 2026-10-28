// @noEmit: true
// The end of the file ends a type or class member (tsc's canParseSemicolon): the list's `}` is what's missing there,
// not a `;` (mutation seed 20261106, interfaceExtendingClass2). The files have no final newline
// @filename: a.ts
interface I {
    a: {
        b: () => { return; };
    }
// @filename: b.ts
type M = { [K in string]: 1
// @filename: c.ts
type T = {
    m(): void
// @filename: d.ts
class C { x = 1
// @filename: e.ts
declare class D { [x: string]: 1