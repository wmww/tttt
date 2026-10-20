// @strict: true
// @noEmit: true

// The template of a generic mapped target is related without IntersectionState.Target (tsc's plain isRelatedTo), so a
// weak member of it still gets the common property check inside an intersection target: `string[]`'s methods aren't
// `Weak`s and `string[]` isn't a `Query<T>` (mikro-orm's OperatorMap<T> & FilterObject<T>)
type Weak = { $eq?: number; $ne?: number };
type Keys<T> = string & { [K in keyof T]-?: K }[keyof T];
type Filter<T> = { [K in Keys<T>]?: Weak | null };
type Query<T> = Weak & Filter<T>;

function f<A>(x: string[], y: { $eq: number }) {
    const a: Query<A> = x;
    const b: Filter<A> = x;
    const c: Query<A> = y;
}
