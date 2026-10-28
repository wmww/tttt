// @target: es2022
// @module: esnext
// @isolatedModules: true
// @noEmit: true
// A value use of `x` after `import x = A.B` resolves A as a value from the import (tsc's markAliasSymbolAsReferenced,
// when the target is a value or nothing): a class, let or enum declared after the import is TS2449/TS2448/TS2450, a type
// TS2693. A first name that is only a type is TS2702 (checkAndReportErrorForUsingTypeAsNamespace), and a type-only
// import there is TS1380 whether the rest resolves or not (bun 90-R10b)

// @filename: t.ts
export interface G1 { x: 1 }

// @filename: a.ts
import c = K.v; c; class K { static v = 1 }
import d = L.v; d; let L = { v: 1 };
import e = En.X.y; e; enum En { X }
import type { G1 as TO } from "./t";
import f = TO.v; f;
function fn(p = g) { } import g = P.v; const P = { v: 1 };
namespace N { export const v = 1 }
import h = N.v; h;
export {};
