//// [test/cases/ttttClassMembersContainedByOffsetQuirk.ts] ////

//// [a.ts]
// Padding so that Base's members sit at offsets inside Mixed's text range in b.ts.
// ..........................................................................................
// ..........................................................................................
export class Base {
    alpha(): void {}
    beta(): void {}
}
export class Far {
    gamma(): void {}
}
//// [b.ts]
import { Base } from "./a";
export const mixin = (sup: typeof Base) => class Mixed extends sup {
    // ..........................................................................................
    // ..........................................................................................
    // ..........................................................................................
    own(): void {}
};
export const mixin2 = (sup: typeof Base) => class extends sup {
    own2(): void {}
};


//// [a.js]
// Padding so that Base's members sit at offsets inside Mixed's text range in b.ts.
// ..........................................................................................
// ..........................................................................................
export class Base {
    alpha() { }
    beta() { }
}
export class Far {
    gamma() { }
}
//// [b.js]
export const mixin = (sup) => class Mixed extends sup {
    // ..........................................................................................
    // ..........................................................................................
    // ..........................................................................................
    own() { }
};
export const mixin2 = (sup) => class extends sup {
    own2() { }
};


//// [a.d.ts]
export declare class Base {
    alpha(): void;
    beta(): void;
}
export declare class Far {
    gamma(): void;
}
//// [b.d.ts]
import { Base } from "./a";
export declare const mixin: (sup: typeof Base) => {
    new (): {
        alpha(): void;
        beta(): void;
        own(): void;
    };
};
export declare const mixin2: (sup: typeof Base) => {
    new (): {
        own2(): void;
        alpha(): void;
        beta(): void;
    };
};
