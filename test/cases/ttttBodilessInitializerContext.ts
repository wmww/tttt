// A parameter initializer of a signature without a body is only TS2371 (checkVariableLikeDeclaration returns
// there), but its type is still checkExpression's, with the parameter's annotation as contextual type; nothing in
// it is checked or reported. Seed 20261116, contextualTypingGenericFunction2 delete-stmt
declare function h(o: { p: 1 | 2, f: (x: number) => void } = { p: 1, f: x => {} }): void;
declare class K { m(o: { f: (x: number) => void } = { f: x => {} }): void; }
interface I { m(o: { f: (x: number) => void } = { f: x => {} }): void; }
type T = (o: { f: (x: number) => void } = { f: x => {} }) => void;
declare function h1(o: { f: (x: number) => void } = { f: x => x.foo }): void;
declare function h2(o: { p: string } = { p: 1, q: 2 }): void;
declare function h3([a, b]: [number, string] = [1, ""]): void;
declare function h5(o = { f: (x: number) => x }): void;
function ov(o: { f: (x: number) => void } = { f: x => {} }): void;
function ov() {}
