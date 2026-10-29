// @strict: true
// A renaming with a computed name in a parameter of a signature without a body says nothing about the name (bun cases
// 03-C11b, 34-C14b; a function type's too, which the harness's declaration emit reports anyway): tsc's
// checkVariableLikeDeclaration returns at the renaming (TS2842) before checkComputedPropertyName
declare function fe({ [undeclared]: z }: any): void;
declare function fe3({ x: { [missing2]: b } }: any): void;
declare class C { m({ [missing3]: c }: any): void; }
function body({ [missing4]: d }: any) { return d; }
declare function fe2({ [missing5]: { e } }: any): void;
// A pattern that types its parameter checks the name when the type is asked for: by an implementation's overload
// check (mutation seed 20261122's mut5487), or a call. Its element is implicitly any only under a key usable as a name
const k = "a";
function fd({ [missing6]: f });
function fd() {}
function fg({ [k]: g });
function fg() {}
