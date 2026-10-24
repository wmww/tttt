// origin: npm deepagent-ltd/deepagent-code https://github.com/deepagent-ltd/deepagent-code packages/app/tsconfig.bench.json seed 20261402 (large)
// flip: yes
// @strict: true
// A conditional type's `infer` doesn't infer through an instantiable source's constraint (tsc's NoConstraints):
// SetStoreFunction<T>'s parameter Part<T> holds a deferred conditional whose constraint mentions T[number], and
// inferring T' = T[number] from it made Signal<T'> not definitely false, so solid-primitives' PersistedState stayed
// deferred (TS2322 on its first element)
type Accessor<T> = () => T;
type Setter<T> = {
    <U extends T>(...args: undefined extends T ? [] : [value: Exclude<U, Function> | ((prev: T) => U)]): undefined extends T ? undefined : U;
    <U extends T>(value: (prev: T) => U): U;
};
type Signal<T> = [get: Accessor<T>, set: Setter<T>];
type Part<T> = keyof T | ([T] extends [readonly unknown[]] ? (item: T[number]) => boolean : never);
interface SetStoreFunction<T> {
    (k1: Part<T>): void;
}
type PersistedState<S> = S extends Signal<infer T> ? [get: Accessor<T>, set: Setter<T>] : S extends [infer T, SetStoreFunction<infer T>] ? [get: T, set: SetStoreFunction<T>] : never;
declare function makePersisted<S>(signal: S): PersistedState<S>;
export function persisted<T>(store: [T, SetStoreFunction<T>]): [T] {
  const [state] = makePersisted(store);
  return [state];
}
