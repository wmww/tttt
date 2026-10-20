// tsc's findMostOverlappyType for an intersection source with a type variable member: its keys are generic
// (`keyof (undefined & T)` is `keyof T`), and `&` a union target member's keys that are a union is a union of
// intersections, which matches with no overlap: the last such member is the one elaborated on (apollo-client's
// `DeepPartialPrimitive & TData` against `DeepPartialPrimitive & CustomMaybeMaskedImplementation<TData>`)
// @strict: true
type P = number | Date | RegExp;
function f<T, U>(x: undefined & T): P & U {
    return x;
}
function g<T, U>(x: { a: string } & T): (Date | { a: number }) & U {
    return x;
}
