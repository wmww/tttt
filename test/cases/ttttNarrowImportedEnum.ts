// An imported enum is narrowed like a variable: `'x' in E` (Symbol.flow_narrowed gates the flow walk)
// @strict: true
// @filename: /m.ts
export enum E { A, B }
// @filename: /a.ts
import { E } from "./m";
if ('extra' in E) {
    const v = E.extra;
}
