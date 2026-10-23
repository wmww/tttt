// @module: esnext
// @noEmit: true

// A type-only import whose module isn't found (TS2307) marks nothing type-only when it's a default import (tsc's
// getTargetOfImportClause returns first): using it as a value is no TS1361. A named one is still marked. One whose chain ends in a missing member further down still is type-only
// @filename: /a.ts
export const v = 1;
// @filename: /index.ts
import type missing from "./nope";
import { type named } from "./nope2";
import type { v } from "./a";
missing;
v;
named;
// @filename: /b.ts
export { B as C } from './a';
// @filename: /c.ts
import type { C } from './b';
export { C as D };
// @filename: /d.ts
import { D } from './c';
new D();
