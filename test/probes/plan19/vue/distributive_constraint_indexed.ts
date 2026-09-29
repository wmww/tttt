// @strict: true
type C<T> = T extends string ? number : T
export function f1<T, K extends keyof T>(a: C<(readonly any[] & T & Record<any, any>)[K]>): T[K] { return a }
export function f2<T, K extends keyof T>(a: C<(readonly any[] & T)[K]>): T[K] { return a }
export function f3<T, K extends keyof T>(a: C<(T & Record<any, any>)[K]>): T[K] { return a }
export function f4<T, K extends keyof T>(a: C<(number[] & T)[K]>): T[K] { return a }
export function f5<T, K extends keyof T>(a: C<(any[] & T)[K]>): T[K] { return a }
