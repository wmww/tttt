// @strict: true
// A parameter pattern of a signature without a body: a renaming is only TS2842 (tsc's checkVariableLikeDeclaration
// returns there), and in an ambient context, a `declare const`'s annotation too, null and undefined are taken out of
// the parent type (getBindingElementTypeFromParentType) (bun's 34-C14a, 09-C10)
type H = ({ x: a }: {}) => void;
declare function g({ y }: {}): void;
declare const f1: ({ a }: { a: 1 } | undefined) => void;
type F4 = ({ a }: { a: 1 } | undefined) => void;
declare const c = () => { { 1; } };
