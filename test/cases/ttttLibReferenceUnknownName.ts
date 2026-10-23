// @strict: true
// @noEmit: true
// @lib: es2022
// A `/// <reference lib>` to no lib name nor lib file name is TS2726, or TS2727 with a suggestion, and loads nothing
// (tsgo's GetLibFileName); a lib file's name, `lib.dom.d.ts`, is that lib (bun 40-C5)
/// <reference lib="" />
/// <reference lib="esnext.full" />
/// <reference lib="es2022x" />
/// <reference lib="lib.dom.d.ts" />
const d: Document = null!;
