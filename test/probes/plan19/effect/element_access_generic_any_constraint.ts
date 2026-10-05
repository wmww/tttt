// @strict: true
type T2<S> = S extends { x: infer A } ? A : never
export const g = <S extends { x: any }>(a: T2<S>) => a[0]
export const g2 = <S extends { x: string }>(a: T2<S>) => a[0]
export const g3 = <S extends { x: string }>(a: T2<S> & {}) => a[0]
