// @declaration: true
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
