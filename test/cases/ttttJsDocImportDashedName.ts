// @allowJs: true
// @checkJs: true
// @noEmit: true
// @strict: true
// The name after a JSDoc tag is the JSDoc scanner's token, whose identifiers take `-` too (tsc's scanJSDocToken):
// `@import a-b from "./p"` is a default import named `a-b`
// @filename: p.ts
export interface Foo {}
// @filename: a.js
/** @import a-b from "./p" */
/** @import c-d, { Foo } from "./p" */
