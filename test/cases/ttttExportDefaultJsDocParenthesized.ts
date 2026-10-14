// @allowJs: true
// @checkJs: true
// @noEmit: true
// A JSDoc `@type` on `export default (e)` is the statement's (checkExportAssignment checks e against it), not a cast of
// the parenthesized e
// @filename: a.js
/** @type {string} */ export default (1);
// @filename: b.js
/** @type {{a: number}} */export default ({ a: 1, b: 2 });
// @filename: c.ts
import A from "./a";
import B from "./b";
const x = A, y = B;
