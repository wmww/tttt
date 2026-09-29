// @strict: true
declare function ev4<Params extends unknown[]>(f: (...params: Params) => unknown, ...args: Params): Params
export const a = ev4((t) => t + 1, 1)
declare function ev5<Params extends unknown[]>(f: (...params: Params) => unknown, args: Params): Params
export const b = ev5((t) => t + 1, [1])
declare function ev6<Params extends unknown[]>(...args: Params): Params
export const c = ev6(1)
declare function ev7<Params extends unknown[]>(args: Params, f: (...params: Params) => unknown): Params
export const d = ev7([1], (t) => t + 1)
