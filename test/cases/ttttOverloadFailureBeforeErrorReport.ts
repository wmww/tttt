// @strict: true
// A call failing on a candidate's first check (generic calls among the arguments left out: tsc's SkipGenericFunctions)
// is taken to return what inferSignatureInstantiationForOverloadFailure infers before the errors are reported; their
// report checks the arguments in full and resolves those calls, but too late: nothing is inferred from them, the type
// parameter is its constraint (opencode's node-build.test.ts, `makeGlobalNode({ layer: Layer.effect(...) })`)
interface Box<A, B> { a: A; b: B }
declare const mk: { <A>(a: A): <B>(b: B) => Box<A, B>; <A, B>(a: A, b: B): Box<A, B> };
declare function node<I extends Box<unknown, unknown>>(input: { layer: I; name: string }): I["a"];
const r = node({ layer: mk(1, 2) });
declare function node2<I extends Box<unknown, unknown>>(layer: I, name: string): I["a"];
const r2 = node2(mk(1, 2));
const r3 = node2(mk(1, 2), 5);
declare function node4<I extends Box<unknown, unknown>>(input: { layer: I; name: string }, x: number): I["a"];
const r4 = node4({ layer: mk(1, 2), name: "" }, "");
const r5 = node({ layer: mk(1, 2), name: 5 });
