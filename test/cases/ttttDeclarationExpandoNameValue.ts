// @declaration: true
// An expando member whose value is a name is `export { v as x }` in the namespace: the local it names is kept
const _skip = Symbol();
const DEFAULT_TIMEOUT = 60 * 1000;
type KeysMap = { a: number };
const keys: KeysMap = { a: 1 };
export function f() {}
f.skip = _skip;
f.DEFAULT_TIMEOUT = DEFAULT_TIMEOUT;
f.keys = keys;
