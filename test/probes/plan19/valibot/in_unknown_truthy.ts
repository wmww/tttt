// @strict: true
function f(value1: unknown, value2: unknown) {
  if (typeof value1 === typeof value2) {
    if (value1 && value2 && value1.constructor === Object) {
      for (const key in value2) {
        if (key in value1) {
        }
      }
    }
  }
}
function g(v: unknown, k: string) {
  if (v) { k in v; }
  if (v && v.constructor === Object) { k in v; }
}
