interface IC<T> {}
type IK<T> = symbol & IC<T>
declare function i1<T>(key: IK<T> | string): T
declare function i2<T>(key: IC<T> | string): T
declare function i3<T>(key: (symbol & IC<T>) | string): T
declare const key: IK<number>
declare const c: IC<number>
export const a = i1(key)
export const b = i2(c)
// TODO: tsc infers number here too (reduce_intersection_target should infer between identical members, see plan 19)
export const d = i3(key)
export const e = i1(c as IK<number>)
