// @strict: true
// A tuple's spread of a conditional or indexed access type stays variadic (tsc's createNormalizedTupleType keeps an
// InstantiableNonPrimitive element Variadic): an array is no match for it (TS2624), as for a type parameter
type Tup<T, N extends number> = number extends N ? T[] : N extends 0 ? [] : [T];
function f<I, S extends number, K extends keyof [I[], I[]]>(a: I[], b: [...Tup<I, S>], c: [...[I[], I[]][K]], d: [...Tup<I, S>, I]) {
    b = a;
    c = a;
    d = a;
    a = b;
}
