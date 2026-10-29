// A bodiless signature's unannotated pattern parameter: checking an element that isn't a renaming asks for the
// parameter's type (getTypeForBindingElementParent from checkVariableLikeDeclaration), which is its initializer's,
// checked and reported (checkDeclarationInitializer), unless the initializer has a quick type (a call of one
// non-generic signature: only its callee is checked). Seed 20261118, objectRestBindingContextualInference
// delete-stmt (a missing TS2554 in a parameter list that swallowed the next statement)
declare function g(a: number): { p: number, q: string };
declare function h<K extends string>(a: number, k: K[]): { p: number, q: K };
declare function f1({ p, ...rest } = g()): void;
declare function f2({ p, ...rest } = h(1, ["x"], 3)): void;
declare function f3({ p } = h(1, ["x"], 3)): void;
declare function f4({ p, ...rest } = h("x", ["x"])): void;
declare function f5({ p: pp } = h("x", ["x"])): void;
declare function f6({ p }: { p: number } = h("x", ["x"])): void;
declare function f7(o = h("x", ["x"])): void;
type T1 = ([a] = [g("x")]) => void;
interface I2 { m([a] = [g("x")]): void }
declare class C3 { m([a] = [g("x")]): void }
declare function f8([] = [g("x")]): void;
declare function f9([, ] = [g("x")]): void;
declare function f10({ a: [b] } = { a: [g("x")] }): void;
declare function f11([a] = [() => { g("x"); }]): void;
function f12([a] = [g("x")]): void;
function f12() {}
