//// [test/cases/ttttDeclarationElidedAnyComputedMember.ts] ////

//// [ttttDeclarationElidedAnyComputedMember.ts]
// A type literal's or interface's member whose computed name isn't late bound (an unresolved import: any) is elided
import type {E} from 'nonexistent-mod';
export interface H { [E]: number; a: 1 }
export type T = { [E]?: string; b: 2 };
export type U = { ["lit"]: 1; [-1]: 2; [`t`]: 3 };
declare const s: unique symbol;
export type V = { [s]: 1 };


//// [ttttDeclarationElidedAnyComputedMember.js]
export {};


//// [ttttDeclarationElidedAnyComputedMember.d.ts]
export interface H {
    a: 1;
}
export type T = {
    b: 2;
};
export type U = {
    ["lit"]: 1;
    [-1]: 2;
    [`t`]: 3;
};
declare const s: unique symbol;
export type V = {
    [s]: 1;
};
export {};
