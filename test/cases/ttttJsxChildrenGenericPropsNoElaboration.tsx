// @jsx: preserve
// @strict: true
// Children aren't elaborated against a generic props type's `P["children"]` (tsc's getIndexedAccessType defers it, and
// elaborateElementwise skips an indexed access): the attributes as a whole are reported
declare namespace JSX { interface Element { e: 1 } interface ElementChildrenAttribute { children: {} } interface IntrinsicElements {} }
function W<P extends { children: number[] }>(C: (p: P) => JSX.Element) { return <C>{"a"}{"b"}</C>; }
function W2<P extends { children: number }>(C: (p: P) => JSX.Element) { return <C>{"a"}</C>; }
function W3<P extends { children: number }>(C: (p: P) => JSX.Element) { return <C x={1}>{"a"}</C>; }
function W4<P extends { children: [number, string] }>(C: (p: P) => JSX.Element) { return <C>{"a"}{1}</C>; }
