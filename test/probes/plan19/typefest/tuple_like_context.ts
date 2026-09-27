// @target: es2015
type ArrayLengthMutationKeys = 'splice' | 'push' | 'pop' | 'shift' | 'unshift';
type FixedLengthArray<Element, Length extends number, ArrayPrototype = [Element, ...Element[]]> = Pick<
  ArrayPrototype,
  Exclude<keyof ArrayPrototype, ArrayLengthMutationKeys>
> & {
  [index: number]: Element;
  [Symbol.iterator]: () => IterableIterator<Element>;
  readonly length: Length;
};
declare function f<T>(x: T): void;
f<FixedLengthArray<string, 3>>(['a', 'b', 'c']);
const x: { 0: number, 1: string } = [1, 'a'];
