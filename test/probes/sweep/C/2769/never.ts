type PredicateFor<T, K extends keyof T, R> = T[K] extends string ? { equals(v: T[K]): R } : { other(): R };
interface Q<T> { where<K extends keyof T>(keyName: K): PredicateFor<T, K, this>; }
declare const q: Q<{}>;
q.where("title").equals("x");
