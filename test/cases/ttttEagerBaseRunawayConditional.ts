// @strict: true
// @noTypesAndSymbols: true
// @noEmit: true
// postcss-selector-parser's shape (vue, vitepress): resolving _Selector<Selector>'s members, tsc instantiates its base
// Container<Diff<Node, S>> with S = Selector, whose distribution relates Root to Selector, which needs
// _Selector<Selector>'s members again: tsc has no in-flight state there and runs away (TS5115)
type Diff<T, U> = T extends U ? never : T;
interface Container<C extends Node = Node> { first: C; }
interface Root extends Container<Root> { type: "root" }
interface _Selector<S> extends Container<Diff<Node, S>> { type: "selector" }
type Selector = _Selector<Selector>;
export type Node = Root | Selector;
function typeOf(s: Selector) {
    return s.type;
}
