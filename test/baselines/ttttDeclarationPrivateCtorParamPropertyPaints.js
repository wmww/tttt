//// [test/cases/ttttDeclarationPrivateCtorParamPropertyPaints.ts] ////

//// [l.ts]
export class Ref { release() {} }
//// [c.ts]
import { Ref } from "./l";
export class M {
    private constructor(readonly ref: Ref) {}
    static make() { return new M(new Ref()); }
}


//// [l.js]
export class Ref {
    release() { }
}
//// [c.js]
import { Ref } from "./l";
export class M {
    ref;
    constructor(ref) {
        this.ref = ref;
    }
    static make() { return new M(new Ref()); }
}


//// [l.d.ts]
export declare class Ref {
    release(): void;
}
//// [c.d.ts]
import { Ref } from "./l";
export declare class M {
    readonly ref: Ref;
    private constructor();
    static make(): M;
}
