// A function's expando members are written as a namespace's, and inside it their names shadow the file's: a type
// naming `visitors` or `verify` can't go through the import of that name when a member has it (babel-traverse's
// `typeof import("./visitors.ts").verify`, `var _a: typeof visitors.verify`)
// @declaration: true
// @filename: /visitors.ts
function verify$1(x: number): void {}
export { verify$1 as verify };
// @filename: /a.ts
import * as visitors from "./visitors";
export { visitors };
export default function traverse(n: number) {}
traverse.visitors = visitors;
traverse.verify = visitors.verify;
// @filename: /b.ts
import * as visitors from "./visitors";
import { verify } from "./visitors";
export default function walk(n: number) {}
walk.verify = visitors.verify;
walk.v2 = verify;
