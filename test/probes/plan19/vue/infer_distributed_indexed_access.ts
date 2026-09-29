// @strict: true
type LooseRequired<T> = { [P in keyof (T & Required<T>)]: T[P] }
declare function h1<Keys extends string = string, Inferred = string extends Keys ? {} : { [k in Keys]?: any }>(
  o: { props?: Keys[]; setup?: (props: LooseRequired<Readonly<Inferred>>) => any }): Inferred
export const a = h1({ props: ['name'], setup(props: { name: 'one' | 'two' }) { return 1 } })
declare function h2<Inferred>(
  o: { setup?: (props: LooseRequired<Readonly<Inferred> & Readonly<{}> & {}>) => any }): Inferred
export const b = h2({ setup(props: { name: 'one' | 'two' }) { return 1 } })
declare function h3<Inferred>(
  o: { setup?: (props: LooseRequired<Readonly<Inferred> & {}>) => any }): Inferred
export const c = h3({ setup(props: { name: 'one' | 'two' }) { return 1 } })
declare function h4<Inferred>(
  o: { setup?: (props: LooseRequired<Readonly<Inferred> & Readonly<{}>>) => any }): Inferred
export const d = h4({ setup(props: { name: 'one' | 'two' }) { return 1 } })
