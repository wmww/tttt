// A tuple's elements are a delimited list (tsc's parseDelimitedList(TupleElementTypes)): a missing `,` is reported and
// the list goes on, a token that can't start a type is skipped (its error at the same position as the `,` one isn't
// reported again). Babel's parser fixtures `[x.y: A]` and `[x?.y: A]`
type A = string;
declare namespace x { type y = number }
type T1 = [x.y: A];
type T2 = [A B];
type T3 = [A, (((A))), (A)[]];
