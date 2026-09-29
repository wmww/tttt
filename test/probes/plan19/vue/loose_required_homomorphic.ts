// @strict: true
type LooseRequired<T> = { [P in keyof (T & Required<T>)]: T[P] }
type K1<T> = keyof (T & Required<T>)
type U = LooseRequired<{ type: 'button'; buttonType?: 'submit' } | { type: 'link'; href: string }>
declare const u: U
declare function f<T>(x: T): LooseRequired<T>
const r = f(u)
if (u.type === 'button') {
  u.buttonType
}
type Arr = LooseRequired<string[]>
