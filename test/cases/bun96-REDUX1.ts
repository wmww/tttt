// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Store<S, T> { getState(): S & T }

// redux, src/createStore.ts: X has no inference site but the contextual return type
declare function createStore<S, E, X>(reducer: S, enhancer: E): Store<S, X> & E
function legacy_createStore<S, E extends {}, X>(reducer: S, enhancer: E): Store<S, X> & E {
  return createStore(reducer, enhancer)
}
// with redux's conditional type, defaults and NoInfer: the words of the project's error
type UnknownIfNonSpecific<T> = {} extends T ? unknown : T
declare function createStore2<S, Ext extends {} = {}, StateExt extends {} = {}>(reducer: S, enhancer?: Ext): Store<S, UnknownIfNonSpecific<StateExt>> & NoInfer<Ext>
function legacy_createStore2<S, Ext extends {} = {}, StateExt extends {} = {}>(reducer: S, enhancer?: Ext): Store<S, UnknownIfNonSpecific<StateExt>> & Ext {
  return createStore2(reducer, enhancer)
}

// the same from an argument; `object`; what is inferred
declare function stateOf<S, X>(store: Store<S, X>): [S, X]
function f1<E extends {}>(store: Store<string, { a: 1 }> & E) { return stateOf(store)[0].length }
function f2<E extends object>(store: Store<string, { a: 1 }> & E) { const v: null = stateOf(store) }
// both tools agree on these two: `Store & unknown` is a reference in both, `Store & { c: 3 }` in neither
function f3<E>(store: Store<string, { a: 1 }> & E) { const v: null = stateOf(store) }
function f4<E extends { c: 3 }>(store: Store<string, { a: 1 }> & E) { const v: null = stateOf(store) }

// other generic types whose type arguments cannot be read off the members
interface Pair<A, B> { either: A | B }
declare function pair<A, B>(p: Pair<A, B>): [A, B]
declare function tuple<T extends unknown[]>(t: [...T]): T
function f5<E extends {}>(p: Pair<1, 2> & E) { const v: null = pair(p) }
function f6<E extends {}>(t: [1, 2] & E) { const v: null = tuple(t) }
const sentinel: number = "";
