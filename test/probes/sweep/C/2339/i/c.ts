type P1<T, K extends keyof T> = T[K] extends any[] ? "arr" : "other";
type IsAny<T> = (T extends never ? true : false) extends false ? false : true;
type P2<T, K extends keyof T> = IsAny<T[K]> extends true ? "any" : "no";
type P3<V> = IsAny<V> extends true ? "any" : "no";
interface Q<T> { a<K extends keyof T>(k: K): P1<T, K>; b<K extends keyof T>(k: K): P2<T, K>; c<K extends keyof T>(k: K): P3<T[K]>; }
declare const q: Q<unknown>;
q.a; q.b; q.c;
