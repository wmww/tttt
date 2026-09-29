interface State<T> { a?: T; }
declare function set1<S, K extends keyof S>(s: S, state: Pick<S, K>): K;
declare function set2<S, T, K extends keyof (S & State<T>)>(s: S, t: T, state: Pick<S & State<T>, K>): K;
class C<S> { set<K extends keyof S>(state: Pick<S, K>): K { return null!; } }
function f<S, T>(s: S, t: T, c: C<S & State<T>>, c2: C<State<T>>) {
    const r1 = set1(s, { a: t });
    const r2 = set2(s, t, { a: t });
    const r3 = c.set({ a: t });
    const r4 = c2.set({ a: t });
}
