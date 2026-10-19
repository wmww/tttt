// @noUnusedLocals: true
// @allowJs: true
// @checkJs: true
// @noEmit: true
// An unused `import type * as N` (and a JSDoc `@import * as N`) is a value declaration's TS6133, its clause's other
// names type declarations' TS6196 (tsc's isTypeDeclaration); an inline `{ type A }` is TS6133

// @filename: t.ts
export interface A {} export const v = 1; export default 1;

// @filename: b.ts
import type * as N1 from './t.js';
import type { A as A1 } from './t.js';
import type D1 from './t.js';
import * as N2 from './t.js';
import { type A as A2, v } from './t.js';
export { v };

// @filename: a.js
/** @import * as N from './t.js' */
/** @import { A } from './t.js' */
/** @import D from './t.js' */
export const x = 1;
