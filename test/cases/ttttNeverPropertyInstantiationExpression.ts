// A property of never is the error type (TS2339 only), which an instantiation expression has nothing to say about,
// unlike any (TS2635) (mutation of symbolToNodeBoundaryNoStackOverflow)
declare const node: never;
type A = typeof node.foo<any>;
const b = node.foo<any>;
const c = node["foo"]<any>;
declare const n2: any;
type C = typeof n2.foo<any>;
