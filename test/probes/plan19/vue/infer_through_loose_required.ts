// @strict: true
type LooseRequired<T> = { [P in keyof (T & Required<T>)]: T[P] }
type ToResolved<P, E> = Readonly<P> & Readonly<E>
declare function dc<Keys extends string = string, Inferred = string extends Keys ? {} : { [k in Keys]?: any }>(
  o: { props?: Keys[]; setup?: (props: LooseRequired<ToResolved<Inferred, {}> & {}>) => any }): Inferred
export const c = dc({ props: ['name'], setup(props: { name: 'one' | 'two' }) { return 1 } })
declare function d2<I>(setup: (props: LooseRequired<I>) => any): I
export const e = d2((props: { name: 'one' | 'two' }) => 1)
declare function d3<I>(setup: (props: LooseRequired<Readonly<I> & {}>) => any): I
export const f = d3((props: { name: 'one' | 'two' }) => 1)
