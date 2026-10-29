// @noImplicitAny: true,false
// @noEmit: true

// A bodiless signature's unannotated pattern is typed from its elements when the parameter's type is asked for
// (an overload's check against its implementation, a call): tsc's getTypeFromObjectBindingPattern checks a
// renamed element's computed key then (TS2464), which checkVariableLikeDeclaration skipped (mutation seed 20261122's
// mut5487)
declare const k: string, o: object;
function f5({ [k in o]: c });
function f5() {}
function f6(a: number, { [k in o]: P });
declare function f7({ [k in o]: c }): void;
function f8({ a: { [k in o]: c } });
function f8() {}
declare function f10({ [k in o]: c }): void;
f10({});
function f11({ [k in o]: c }: {});
function f11() {}
