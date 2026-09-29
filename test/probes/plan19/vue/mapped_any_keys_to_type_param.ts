// @strict: true
type M<T> = { [P in keyof T]: T[P] }
type N<T> = { [P in keyof T]: number }
export function f1<T>(a: M<T & Record<any, any>>): T { return a }
export function f2<T>(a: N<T & Record<any, any>>): T { return a }
export function f3<T>(a: M<T & object>): T { return a }
export function f4<T>(a: keyof (T & Record<any, any>)): number { return a }
export function f5<T>(a: N<Record<any, any>>): T { return a }
