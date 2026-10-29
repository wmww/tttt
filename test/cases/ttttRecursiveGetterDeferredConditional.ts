// A conditional type met while a getter's inferred return type is in flight (the variable it returns from circular):
// tsgo resolves `Out<Box<Box<any>>>` to unknown (`o` is optional): a missing or optional required property says false
// without the getter's type (seed 20261119, recursiveTypeInference2 add-optional: a spurious TS2769 on zod-like
// `.default([])`)
type Out<T> = T extends { o: any } ? T["o"] : unknown;
interface Box<T> { o?: T; get(): Out<this>; }
declare function box<T>(v: T): Box<T>;
declare function obj<T>(shape: T): Box<T>;
const Tree = obj({
  name: 1,
  get children() {
    return box(Tree).get();
  },
});
declare const o1: Out<Box<typeof Tree>>;
