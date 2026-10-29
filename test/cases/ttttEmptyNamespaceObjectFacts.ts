// @strict: true
// The object of a namespace without values is an empty anonymous object to tsc, whose type facts keep it under
// `typeof x !== "object"` and `!x` (bun cases 16-C5a, 16-C5b)
// @filename: empty.ts
export type T = number;
// @filename: a.ts
import * as e from "./empty";
function n(x: never) {}
namespace N { const x = 1; }
const nn = N;
if (typeof nn !== "object") n(nn);
if (!nn) n(nn);
if (typeof e !== "object") n(e);
enum E {}
const ee = E;
if (!ee) n(ee);
namespace V { export const v = 1; }
const vv = V;
if (!vv) n(vv);
