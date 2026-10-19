// A namespace import is narrowed by a `this` predicate of a function it exports (`N.isTagged()`): the callee's object
// counts (Symbol.flow_narrowed_this) where the namespace's exported functions declare one
// @strict: true
// @filename: /m.ts
export namespace N {
    export const a = 1;
    export function isTagged(this: unknown): this is { tag: string } { return true; }
}
export namespace P {
    export const b = 1;
    export function check(x: unknown): x is number { return true; }
}
// @filename: /a.ts
import { N, P } from "./m";
if (N.isTagged()) {
    const t: string = N.tag;
}
declare const u: unknown;
if (P.check(u)) {
    const n: number = u;
}
