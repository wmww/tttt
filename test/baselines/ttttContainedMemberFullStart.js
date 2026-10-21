//// [test/cases/ttttContainedMemberFullStart.ts] ////

//// [base.ts]
export class Base {
    declare a: number;
    // ------------------------------------------------------------ a long comment before b
    declare b: number;
    declare c: number;
}
//// [mixin.ts]
//xxxxxxxxxxxxxxxx
import { Base } from "./base";
export const mix = (sc: typeof Base) =>
    class M extends sc {
        own = 1;
        // yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy
    };




//// [base.d.ts]
export declare class Base {
    a: number;
    b: number;
    c: number;
}
//// [mixin.d.ts]
import { Base } from "./base";
export declare const mix: (sc: typeof Base) => {
    new (): {
        c: number;
        own: number;
        a: number;
        b: number;
    };
};
