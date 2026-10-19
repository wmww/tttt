// An imported namespace and a namespace import are narrowed like variables: `'x' in N`, `'x' in B`
// @strict: true
// @filename: /m.ts
export namespace N { export const a = 1; }
// @filename: /b.ts
export const b = 1;
// @filename: /a.ts
import { N } from "./m";
import * as B from "./b";
if ('extra' in N) {
    const v = N.extra;
}
if ('more' in B) {
    const w = B.more;
}
