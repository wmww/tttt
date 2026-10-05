type IsStrictlyAny<T> = (T extends never ? true : false) extends false ? false : true;
interface Q<T> { where<K extends keyof T>(keyName: K): P<T, K>; }
type P<T, K extends keyof T> = IsStrictlyAny<T[K]> extends true ? "any" : T[K] extends any[] ? "arr" : "other";
declare const q: Q<unknown>;
// @ts-ignore
const r = q.where("title");
type A = IsStrictlyAny<never>;
type B = P<unknown, never>;
