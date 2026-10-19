// An imported function is narrowed like a variable (tsc's checkIdentifier with isAlias): `'x' in f`, a predicate and an
// assertion on it. Its flow is walked only because a narrowing node names it (Symbol.flow_narrowed)
// @strict: true
// @filename: /m.ts
export declare function f(): void;
export declare function isString(x: unknown): x is string;
export declare function assertNumber(x: unknown): asserts x is number;
// @filename: /a.ts
import { f, isString, assertNumber } from "./m";
if ('cache' in f) {
    f.cache = 1;
}
declare const u: unknown;
if (isString(f)) {
    const s: string = f;
}
function g() {
    assertNumber(f);
    return f;
}
