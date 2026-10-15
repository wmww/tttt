//// [test/cases/ttttDeclarationExpandoHostComments.ts] ////

//// [ttttDeclarationExpandoHostComments.ts]
// A function expression with expandos is a function declaration made anew: without the statement's comments; also
// when an export list exports it
/**
 * doc of W
 */
export const W = ({ a }: { a: number }) => a; /** trailing */
W.displayName = "W";
/** doc of F */
export function F() {}
F.displayName = "F";
/** doc of G */
const G = function () {};
G.x = 1;
export { G };


//// [ttttDeclarationExpandoHostComments.js]
// A function expression with expandos is a function declaration made anew: without the statement's comments; also
// when an export list exports it
/**
 * doc of W
 */
export const W = ({ a }) => a; /** trailing */
W.displayName = "W";
/** doc of F */
export function F() { }
F.displayName = "F";
/** doc of G */
const G = function () { };
G.x = 1;
export { G };


//// [ttttDeclarationExpandoHostComments.d.ts]
export declare function W({ a }: {
    a: number;
}): number;
export declare namespace W {
    var displayName: string;
}
/** doc of F */
export declare function F(): void;
export declare namespace F {
    var displayName: string;
}
declare function G(): void;
declare namespace G {
    var x: number;
}
export { G };
