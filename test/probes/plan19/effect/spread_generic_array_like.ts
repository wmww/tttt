// @strict: true
// @target: es2022
export const f = <A extends ReadonlyArray<unknown>, B>(self: A, that: B): [...A, B] => [...self, that]
declare function dual<DataLast extends (...args: Array<any>) => any, DataFirst extends (...args: Array<any>) => any>(arity: Parameters<DataFirst>["length"], body: DataFirst): DataLast & DataFirst
export const g: {
  <B>(that: B): <A extends ReadonlyArray<unknown>>(self: A) => [...A, B]
  <A extends ReadonlyArray<unknown>, B>(self: A, that: B): [...A, B]
} = dual(2, <A extends ReadonlyArray<unknown>, B>(self: A, that: B): [...A, B] => [...self, that])
