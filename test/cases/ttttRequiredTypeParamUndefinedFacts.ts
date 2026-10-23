// `-?` takes undefined out with getTypeWithFacts(NEUndefined) (tsc's removeMissingOrUndefinedType): a T constrained
// to undefined goes too, and void
interface O<T> { a?: T }
function f1<T extends undefined>(r: Required<O<T>>) { const n: never = r.a; }
function f2<T extends string | undefined>(r: Required<O<T>>) { return r.a; }
function f3(r: Required<O<void | number>>) { return r.a; }
type R = Required<[(string | undefined)?]>;
const sentinel: number = "";
