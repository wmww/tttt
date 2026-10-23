// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "esnext", "module": "esnext", "lib": ["esnext"], "types": [], "moduleDetection": "force"}, "files": ["neighbours.ts", "then_on_a_union.ts"]}
// @filename: neighbours.ts
export {}
// A call through a union of two generic signatures: the return types are united first.
declare const lists: number[] | ReadonlyArray<number>
export const mapped: never = lists.map(n => String(n))
declare const sets: Set<number> | Map<number, number>
export const each: never = sets.forEach(v => v)
declare const thenables: Promise<number> | PromiseLike<number> | Promise<string>
export const three: never = thenables.then(v => v)
declare const fns: (<T>(value: T) => Promise<T>) | (<U>(value: U) => PromiseLike<U>)
export const called: never = fns(1)
declare const fns2: (<T>(value: T) => T[]) | (<U>(value: U) => ReadonlyArray<U>)
export const called2: never = fns2('a')
declare const one: (<T>(value: T) => Promise<T>) | ((value: number) => PromiseLike<number>)
export const mixed: never = one(1)
interface A { run<T>(value: T): { a: T } }
interface B { run<U>(value: U): { a: U; b: U } }
declare const ab: A | B
export const shapes: never = ab.run(1)
declare const ba: B | A
export const shapes2: never = ba.run(1)
declare const both: A & B
export const overloads: never = both.run(1)
async function settle(p: Promise<number> | PromiseLike<number>) {
  const r = p.then(v => v + 1, () => 'failed' as const)
  const probe: never = r
  if ('finally' in r) r.finally(() => {})
  return await r
}
export const settled: never = settle(Promise.resolve(1))
export const sentinel: number = ''
// @filename: then_on_a_union.ts
export {}
declare const p: Promise<number> | PromiseLike<number>
const r = p.then(v => {})
export const probe_r: never = r
if ('catch' in r) {
  const narrowed: never = r
  r.catch(reason => {})
}
class Holder<T> {
  promise: Promise<T> | PromiseLike<T>
  constructor(arg: Promise<T> | PromiseLike<T>) {
    this.promise = arg
    const thenReturn = this.promise.then(value => {})
    const probe: never = thenReturn
    if ('catch' in thenReturn) {
      const narrowed: never = thenReturn
      thenReturn.catch(reason => {})
    }
  }
}
declare const q: Promise<void> | PromiseLike<void>
if ('catch' in q) { const narrowed: never = q }
declare const u: PromiseLike<void> | Promise<void>
const r2 = u.then(() => 1)
export const probe_r2: never = r2
declare const both: { then(cb: () => void): Promise<void> } | { then(cb: () => void): PromiseLike<void> }
export const probe_both: never = both.then(() => {})
