// @strict: true
type A<T> = T extends readonly [infer U, ...infer V] ? ['first', U, V]
  : T extends readonly [...infer U, infer V] ? ['last', U, V] : 'none';
type R1 = A<[...string[], number]>;
type R2 = A<readonly [...string[], number]>;
type B<T> = T extends readonly [...infer U, infer V] ? [U, V] : 'none';
type R3 = B<[...string[], number]>;
