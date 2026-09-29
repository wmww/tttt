// @strict: true
declare class Brand {
  private __brand?: never
  protected prot: number
  pub: string
}
type K = keyof Brand
type S = { a: { title: string }; b: { title: string } } & Brand
declare const value: S[keyof S]
const t = value.title
function f<T extends object>(x: T): keyof (T & Brand) { return null! }
type P = Partial<Brand>
declare const p: P
declare function g<T>(x: T): { [K in keyof T]: T[K] }
const q = g(p)
