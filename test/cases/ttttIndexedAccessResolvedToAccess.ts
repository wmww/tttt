// @strict: true
// @noEmit: true
// tsc's checkIndexedAccessType checks the index of the node's type: an access that resolves to another, deferred, access
// (`{ o: { a: 1 }[D] }["o"]` is `{ a: 1 }[D]`) has that one's TS2536 again, at the outer node (found by mutation of
// arrayFakeFlatNoCrashInferenceDeclarations)
type X1<D extends number> = {obj: {a: 1}[D extends 1 ? 42 : "a"]};
type X2<D extends number> = {obj: {a: 1}[D extends 1 ? 42 : "a"]}["obj"];
type X3<D extends number> = {obj: {a: 1}[D]}["obj"];
type X4<D extends number> = [{a: 1}[D]][0];
type X5<D extends number> = {obj: {a: 1}[D]};
type X6<T extends { o: unknown }> = T["o"];
type X7<D extends number> = { o: { a: 1 }[D] }["o"]["a"];
type X8 = { o: { a: 1 }["a"] }["o"];
interface I<D extends string> { o: { a: 1 }[D] }
type X9<D extends string> = I<D>["o"];
