// @strict: true
interface Opt<A> { readonly _A: (_: never) => A }
interface Eff<A> { readonly _A: (_: never) => A }
declare function some<A>(a: A): Opt<A>;
declare function none<A = never>(): Opt<A>;
declare function map<A, B>(self: Eff<A>, f: (a: A) => B): Eff<B>;
declare const nextInt: Eff<number>;
declare function unfold<A, S>(s: S, f: (s: S) => Eff<Opt<readonly [A, S]>>): A;
const r1 = unfold(0, (n) => map(nextInt, (i) => none()));
const r2 = unfold(0, (n) => map(nextInt, (i) => n ? some([i, n + 1] as const) : none()));
