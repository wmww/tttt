// @strict: true
// tsc's getReducedType takes a type variable's apparent type among an intersection's properties: D constrained to
// definitions whose `type` is an enum member intersected with { type: "query" } (a string literal, disjoint from the
// enum member) is never, which satisfies any constraint (redux-toolkit's `Definitions[EndpointName] & { type: "query" }`)
enum DT { query = "query", mutation = "mutation" }
interface Base { x: 1 }
type QD = Base & { type: DT.query };
type MD = Base & { type: DT.mutation };
type Q<D extends QD> = D;
type T2<D extends QD | MD> = Q<D & { type: "query" }>;
type T5<D extends QD | MD> = Q<D & { type: DT.query }>;
type Defs = Record<string, QD | MD>;
type T6<Definitions extends Defs, K extends keyof Definitions> = Q<Definitions[K] & { type: "query" }>;
function f<D extends QD | MD>(d: D & { type: "query" }) { const n: never = d; return n; }
