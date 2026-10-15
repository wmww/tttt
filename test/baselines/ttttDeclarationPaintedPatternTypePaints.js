//// [test/cases/ttttDeclarationPaintedPatternTypePaints.ts] ////

//// [index.d.ts]
declare namespace internal { class Readable { read(): void } }
export = internal;
//// [router.ts]
import Stream from "stream";
const { Readable } = Stream;
export class Router { make(): InstanceType<typeof Readable> { return null!; } }


//// [router.js]
import Stream from "stream";
const { Readable } = Stream;
export class Router {
    make() { return null; }
}


//// [router.d.ts]
import Stream from "stream";
declare const { Readable }: typeof Stream;
export declare class Router {
    make(): InstanceType<typeof Readable>;
}
export {};
