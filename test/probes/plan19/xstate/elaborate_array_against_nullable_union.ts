// @strict: true
type SingleOrArray<T> = readonly T[] | T;
declare function f<T extends string>(x: { types: { tags: T }, tags?: SingleOrArray<T> }): void;
f({ types: {} as { tags: 'a' | 'b' }, tags: ['other'] });
const y: { tags?: SingleOrArray<'a' | 'b'> } = { tags: ['other'] };
