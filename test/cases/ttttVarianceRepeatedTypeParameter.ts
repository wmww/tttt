// A repeated type parameter name is one symbol, whose type parameter is the first one: each declaration checks its
// variance annotation against that one (tsc's checkVarianceAnnotations), TS2636 twice (missing_checks.md)
type B<in T, in T> = { a: T };
interface I<in T, in T> { a: T }
type C<out T, in U, out U> = { a: T, b: (u: U) => void };
