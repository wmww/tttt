// @strict: true
type Simplify<T> = {[K in keyof T]: T[K]};
const r1: {a: string} = {} as Simplify<{[x: string]: string} & {a: string}>;
const r2: {a: string} = {} as Simplify<{[x: string]: string; a: string}>;
