// tsc merges a file's declarations of a name across files as one symbol (mergeSymbol of the file's symbol into the
// globals, an ambient module's exports): when they conflict, they do as a whole. Every declaration of both sides is
// reported (one code for all: TS2451 when either side has a block-scoped variable), the file's own declarations stay
// merged with each other (no TS2720 for `class C implements E` + `interface C extends E`), what the file declared
// before the conflicting one doesn't merge into the other file's symbol (an interface next to a `var` conflicting with
// a namespace: `: N` is still the namespace, TS2709), and a conflict with a namespace is TS2649 at the file's first
// declaration (DefinitelyTyped's @types/node twice, @webgpu/types against lib.dom)
// @filename: first.d.ts
declare module "m" {
    interface Events { on(): void }
    class C implements Events { x: number }
    interface C extends Events {}
    const D: number;
    namespace S {}
    const S: number;
}
declare module "e" {
    function e(): void;
    export = e;
}
declare namespace N { const A: number }
// @filename: second.d.ts
declare module "m" {
    interface Events { on(): void }
    class C implements Events { y: number }
    interface C extends Events {}
    const D: string;
    type D = number;
    namespace S {}
    const S: number;
}
declare module "e" {
    function f(): void;
    export = f;
}
interface N { readonly A: number }
declare var N: N;
declare const n: N;
// @filename: use.ts
import { C } from "m";
declare const c: C;
c.on();
