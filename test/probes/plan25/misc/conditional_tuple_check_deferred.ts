// @strict: true
// Storybook: type StoryObj<T> = [T] extends [{ render?: ..; component?: infer C; args?: infer D }] ? .. : ..
// tsc defers a conditional type whose check and extends types are tuples of the same arity with a generic
// element (isDeferredType with checkTuples). tttt resolves `[T] extends [W]` at the declaration: an unconstrained
// T relates to the weak W (through {}), so the alias is fixed to the true branch for every T
type W = { render?: (a: any) => any; component?: unknown };
type C2<T> = [T] extends [W] ? "meta" : "other";
declare function Spinner(props: { size?: string }): void;
const f2: C2<typeof Spinner> = "other";
type C5<T> = [T] extends [{ component?: infer C }] ? C : "other";
const f5: C5<typeof Spinner> = "other";
const f6: C5<{ component: 1 }> = 1;
type IsMeta<T> = [T] extends [{ render?: (a: any) => any; component?: infer C; args?: infer D }] ? "meta" : "other";
const a: IsMeta<typeof Spinner> = "other";
