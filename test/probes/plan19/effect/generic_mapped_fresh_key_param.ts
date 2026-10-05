// @strict: true
type ReadonlyRecord<K extends string | symbol, A> = { readonly [P in K]: A }
declare function map<K extends string, A, B>(self: ReadonlyRecord<K, A>, f: (a: A, key: K) => B): Record<K, B>
interface Field { a: 1 }
type Fields = { readonly [x: string]: Field }
export const f = <F extends Fields>(fields: Readonly<F>) => map(fields, (field) => field.a)
