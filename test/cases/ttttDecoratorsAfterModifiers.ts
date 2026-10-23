// @noEmit: true
// Decorators after the modifiers (tsc's parseModifiers: leading decorators, modifiers, trailing decorators, modifiers)
// on what can't be decorated are TS1206, not a parse error; a declare class's decorators aren't ambient (no TS1183)
declare const dec: any;
@dec function f1() {}
@dec const x = 1;
@dec export function f2() {}
export @dec function f3() {}
export @dec const x3 = 1;
@dec export @dec function f4() {}
@dec export const x4 = 1;
@dec export declare class C1 {}
export @dec declare class C2 {}
@dec(() => {}) declare class C3 {}
export @dec(function () {}) declare class C4 {}
