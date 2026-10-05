// @strict: true
interface Opt<A> { readonly _A: (_: never) => A }
interface Eff<A> { readonly _A: (_: never) => A }
declare function succeed<A>(value: A): Eff<A>;
declare function none<A = never>(): Opt<A>;
declare function unfold<S, A>(s: S, f: (s: S) => Eff<Opt<readonly [A, S]>>): A;
const r1 = unfold(0, (n) => succeed(none()));
const r2 = unfold(0, (n) => n < 10 ? succeed(none()) : succeed(none()));
const r3 = unfold(0, (n) => { return succeed(none()); });
