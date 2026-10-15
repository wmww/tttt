//// [test/cases/ttttDeclarationTypeComments.ts] ////

//// [ttttDeclarationTypeComments.ts]
// The comments inside a reused annotation: each node's JSDoc-style leading and trailing ones (once: a node ending where
// its parameter, variable or member does leaves them to it), and the unfiltered ones after a list delimiter
export interface O {
    cascade?: boolean /** @default true */;
    kind: string | undefined /** How */;
    a: number /* plain */;
    b: number; /** after */
    c: { d: A /** last */ }
}
export type L = 0 /** Facebook */ | 1 /** Google */ | 2;
export type S = A | /* B | */ C | D;
export type W = /* daily */ "x" | /* general */ "y";
type A = 1; type B = 2; type C = 3; type D = 4;
export declare function f(x: A /** px */, y?: B /* py */): C /** ret */;
export declare let v: A /** v */;
export declare let t: [A, B, /* named */ C];
export type F = (x: A /** fx */) => B;
export interface P {
    replacer?: // eslint-disable-next-line
    ((x: any) => any) | number[];
    id: A["toString"] | /** for pending */ null;
}
export type R = readonly (A | /* pending */ {
    x: 1;
})[];
type R = 1; type S = 2;
export type GlobalState = [
  /** Event revalidators */
  R,
  /** Mutation */
  S /** after */,
  /** named */
  n: R,
  ...rest: S[]
];


//// [ttttDeclarationTypeComments.js]
export {};


//// [ttttDeclarationTypeComments.d.ts]
export interface O {
    cascade?: boolean /** @default true */;
    kind: string | undefined /** How */;
    a: number;
    b: number; /** after */
    c: {
        d: A; /** last */
    };
}
export type L = 0 /** Facebook */ | 1 /** Google */ | 2;
export type S = A | /* B | */ C | D;
export type W = /* daily */ "x" | /* general */ "y";
type A = 1;
type B = 2;
type C = 3;
type D = 4;
export declare function f(x: A /** px */, y?: B): C /** ret */;
export declare let v: A /** v */;
export declare let t: [A, B, /* named */ C];
export type F = (x: A /** fx */) => B;
export interface P {
    replacer?: // eslint-disable-next-line
    ((x: any) => any) | number[];
    id: A["toString"] | /** for pending */ null;
}
export type R = readonly (A | /* pending */ {
    x: 1;
})[];
type R = 1;
type S = 2;
export type GlobalState = [
    /** Event revalidators */
    R,
    /** Mutation */
    S /** after */,
    /** named */
    n: R,
    ...rest: S[]
];
export {};
