// @strict: true
class SubArray<T> extends Array<T> {
  last: T | undefined
}
export const s = new SubArray(4, 5, 6)
export const t = new SubArray<string>(4)
class Plain extends Array {}
export const u = new Plain(1, 2)
