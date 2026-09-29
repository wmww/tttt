// @strict: true
interface A { a?: number; onError?: (e: Error) => void }
interface B extends A { b?: string }
export function f(options?: B) {
  if (options) {
    let key: keyof B
    for (key in options) {
    }
  }
}
