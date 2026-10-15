//// [test/cases/ttttDeclarationReadonlyEnumFieldPaints.ts] ////

//// [e.ts]
export const enum Id { A = "a", B = "b" }
//// [c.ts]
import { Id } from "./e";
class P { public readonly id = Id.A; }
export class C { get() { return new P(); } }


//// [e.js]
export {};
//// [c.js]
class P {
    id = "a" /* Id.A */;
}
export class C {
    get() { return new P(); }
}


//// [e.d.ts]
export declare const enum Id {
    A = "a",
    B = "b"
}
//// [c.d.ts]
import { Id } from "./e";
declare class P {
    readonly id = Id.A;
}
export declare class C {
    get(): P;
}
export {};
