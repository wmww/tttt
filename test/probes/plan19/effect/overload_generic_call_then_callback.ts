// @strict: true
interface Eff<A> { readonly _A: (_: never) => A }
interface Deq<out A> extends Eff<A> { readonly _D: (_: never) => A }
interface Queue<in out A> extends Deq<A> { q: (a: A) => A }
declare function bounded<A>(): Eff<Queue<A>>;
declare function run<A>(q: Queue<A>): Eff<void>;
declare const tap: {
  <A, X>(f: (a: A) => Eff<X>, options: { onlyEffect: true }): (self: Eff<A>) => Eff<A>
  <A, X>(self: Eff<A>, f: (a: A) => X): Eff<A>
};
declare const acquireRelease: {
  <A>(release: (a: A) => Eff<void>): (acquire: Eff<A>) => Eff<A>
  <A>(acquire: Eff<A>, release: (a: A) => Eff<void>): Eff<A>
};
export const t2 = <A,>(): Eff<Deq<A>> =>
  tap(acquireRelease(bounded<A>(), (queue) => run(queue)), (queue) => run(queue));
