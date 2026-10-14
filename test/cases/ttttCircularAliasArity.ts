// A type alias whose resolution is circular has no type parameters (tsc's getDeclaredTypeOfTypeAlias): a reference
// without type arguments isn't TS2314, one with them is TS2315, and the alias is resolved before its arguments count
type T2<X> = T2;
type T3<out X> = T3;
type T4<X> = T4<X>;
type T5<X> = { a: T5<X> } | T6<X>;
type T6<X> = T6;
let a: T2<number>;
let b: T2;
