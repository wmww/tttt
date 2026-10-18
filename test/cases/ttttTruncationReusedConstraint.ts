// A signature's constraint reused from its declaration with the instantiation's mapper counts as the node's range
// (`<K extends T>`: 2), whatever its instantiation prints (tsc's tryReuseExistingNodeHelper; rxjs's groupBy overloads)
// @noErrorTruncation: false
// @strict: true
// @target: es2022
interface Subj<T> { next(v: T): void }
interface Obs<T> { sub(): void;
  grp: {
    <K extends T>(keySelector: (value: T) => value is K): Obs<KeyedGroupObservable<true, K> | KeyedGroupObservable<false, Exclude<T, K>>>;
    <K>(keySelector: (value: T) => K): Obs<KeyedGroupObservable<K, T>>;
    <K>(keySelector: (value: T) => K, options: GroupByOptions<K, T, T>): Obs<KeyedGroupObservable<K, T>>;
    <K, E>(keySelector: (value: T) => K, options: GroupByOptions<K, T, E> & { element: (value: T) => E }): Obs<KeyedGroupObservable<K, E>>;
    <K, E>(keySelector: (value: T) => K, element: (value: T) => E, duration?: (group: KeyedGroupObservable<K, E>) => unknown, connector?: () => Subj<E>): Obs<KeyedGroupObservable<K, E>>;
  };
}
interface KeyedGroupObservable<K, T> extends Obs<T> { readonly key: K; }
interface GroupByOptions<K, T, E = T> {
  element?: (value: T) => E;
  duration?: (group: KeyedGroupObservable<K, E>) => unknown;
  connector?: () => Subj<E>;
}
declare const a: Obs<string[]>;
const b: Obs<(string | number)[]> = a;
