// @strict: true
// A function expression assigned to a parenthesized property access gets no contextual `this` from its object (bun
// 07-C5, 32-C2): tsc's getContextualThisParameterType walks up the parentheses around the function, not those around
// the target, which must be an access expression itself
export {};
interface O { x: any; y: number }
declare const obj: O;
(obj.x) = function () { return this.y; };
obj.x = (function () { return this.y; });
((obj.x)) ??= function () { return this.y; };
