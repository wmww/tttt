// @declaration: true
// A binding pattern parameter with an initializer, of a function whose return type is written: its type is the
// signature's parameter's (resolved: the written return type didn't need it)
export function notFound({ verbose = false } = {}): void {}
export function g({ a, b = 1 }: { a: string; b?: number } = { a: "" }): void {}
export function h([x, y] = [1, 2]): void {}
export function k({ a: { b } } = { a: { b: 1 } }): void {}
export const arrow = ({ verbose = false } = {}) => verbose;
export class C { m({ v = 1 } = {}) {} }
