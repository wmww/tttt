// A class's `<` commits to a type parameter list (tsc's parseTypeParameters): a token that can't start one is TS1139
// and skipped unless a list being parsed takes it; `(`, `{`, extends and implements end it
class A< {}
class B<> {}
class C<T, %> {}
const D = class <1> {};
class E< extends X {}
class F<T,
