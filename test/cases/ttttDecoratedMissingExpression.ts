// Decorators on no expression are a missing declaration that the member, call and binary expression parsers go on
// with (tsc's parseDecoratedExpression): `@x as T` is an `as` expression of it
declare function mk<A>(a: A): A[];
function g<T>() { return @null! as typeof mk<number>|; }
