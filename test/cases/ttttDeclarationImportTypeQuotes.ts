// @declaration: true
// @strict: true
// An import type of another file the node builder reuses keeps its quotes when its specifier stays the same
// @filename: c.ts
export interface X { a: 1 }
// @filename: a.d.ts
export declare function mk(): [{ $props: import('./c').X; n: number }];
// @filename: b.ts
import { mk } from "./a";
const [p] = mk();
export { p };
