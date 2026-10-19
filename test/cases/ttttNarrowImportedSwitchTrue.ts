// `switch (true)` clauses and a conditional's condition narrow an imported function too
// @strict: true
// @filename: /m.ts
export declare function f(): void;
export declare function isString(x: unknown): x is string;
// @filename: /a.ts
import { f, isString } from "./m";
switch (true) {
    case 'cache' in f:
        f.cache;
        break;
}
const r = isString(f) ? f : 0;
