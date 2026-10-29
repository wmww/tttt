// @strict: true
// An intersection source's extra property check runs only when its apparent type is structured (tsc's
// isSourceIntersectionNeedingExtraCheck): `T & null` is null, comparable to Map by its T alone, while `T & {}` is `{}`
// and lacks Set's properties (webpack's FileSystemInfo.js, a narrowed `T | undefined` asserted to `Set | Map`)
function f<T>(m: T | undefined) {
  if (m !== undefined) {
    const k = (m as Set<string> | Map<string, any>).keys();
  }
}
function g<T>(m: T & {}) {
  const k = m as Set<string>;
}
