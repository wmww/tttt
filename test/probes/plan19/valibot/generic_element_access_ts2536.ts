// @strict: true
interface E1 { [k: string]: number }
interface E2 { [k: string]: string }
type S = { kind: 1; entries: E1 } | { kind: 2; entries: E2 };
function f<const T extends S, const K extends readonly [keyof T['entries'], ...(keyof T['entries'])[]]>(schema: T, keys: K) {
  for (const key of keys) {
    const v = schema.entries[key];
  }
  const e = schema.entries;
}
