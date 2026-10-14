// @strict: true
// @noEmit: true
// A union's signatures `(...p: T) => void` (T generic) and `(...p: [U, ...unknown[]]) => void` don't match (tsc's
// compareSignaturesIdentical reads `T[0]` at position 0, getTypeAtPosition), so they are combined, with the raw
// minArgumentCount of 0: a spread argument is fine (vueuse's createEventHook `fn(...args)`, fn a conditional
// `Callback<T>` whose branches are rest signatures)
export function f<T extends any[], U>(args: U[], a2: T, a3: [U, ...unknown[]]) {
  let fn1!: ((...p: [U, ...unknown[]]) => void);
  fn1(...args);
  let fn2!: ((...p: unknown[]) => void) | ((...p: [U, ...unknown[]]) => void);
  fn2(...args);
  let fn3!: ((...p: T) => void) | ((...p: [U, ...unknown[]]) => void);
  fn3(...args);
  let fn4!: ((...p: any) => void) | ((...p: [U, ...unknown[]]) => void);
  fn4(...args);
}

type IsAny<T> = 0 extends 1 & T ? true : false;
type Callback<T> = IsAny<T> extends true
  ? (...param: any) => void
  : (
      [T] extends [void]
        ? (...param: unknown[]) => void
        : [T] extends [any[]]
            ? (...param: T) => void
            : (...param: [T, ...unknown[]]) => void
    )
export type EventHookTrigger<T = any> = (...param: Parameters<Callback<T>>) => Promise<unknown[]>
export function createEventHook<T = any>() {
  const fns: Set<Callback<T>> = new Set()
  const trigger: EventHookTrigger<T> = (...args) => {
    return Promise.all(Array.from(fns).map(fn => fn(...args)))
  }
}
