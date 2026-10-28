// `{ x = 1 }` outside a destructuring target is TS1312 and its name means nothing (tsc's checkShorthandPropertyAssignment
// checks the initializer only): no TS2693 for a type, TS2708 for a namespace, TS18004 for nothing
const o1 = { zz = 1 };
type T = number;
const o2 = { T = 1 };
const o3 = { never = 1 };
namespace N { export type X = 1; }
const o4 = { N = 1 };
