// Cross-file merges into an import alias are tsc's mergeSymbol's: an alias that resolves to nothing gives way, one whose
// target doesn't exclude the declaration merges it into the target (a namespace, an interface), one whose target
// excludes it is a conflict and the declaration takes the name
// @filename: a.ts
namespace N { export const x = 1; }
class K {}
import n = N;
import k = K;
import u = Missing.Thing;
interface I {}
import i = I;
// @filename: b.ts
namespace n { export const y = 2; }
var k: number;
function u() {}
interface i { z: number }
const r1 = n.y;
const r2 = u();
// @filename: c.ts
namespace n { export const w = 3; }
const r3 = n.x + n.w;
