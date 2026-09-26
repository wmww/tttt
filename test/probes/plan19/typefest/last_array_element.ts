// @strict: true
type L<E extends readonly unknown[]> = E extends readonly [...infer U, infer V] ? V : 0;
declare function lastOf<V extends readonly any[]>(array: V): L<V>;
declare const t: ['foo', 2];
const a = lastOf(t);
type B = L<['foo', 2]>;
