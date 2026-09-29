// @strict: true
// @noImplicitAny: true
type Slots<T> = { [K in keyof T as `slot-${K & string}`]?: (props: { item: T }) => any } & { label?: (props: { item: T }) => any }
function f<T extends Record<string, any>>(item: T, key: keyof T & string, key2: keyof T) {
  const slots: Readonly<Slots<T>> & Slots<T> = null!
  slots[`slot-${String(key)}`]?.({ item })
  const s1 = slots[`slot-${String(key2)}`]
  for (const k in item) {
    slots[`slot-${String(k)}`]
  }
}
function g<T extends { foo: 'foo'; bar: 'bar' }>(item: T) {
  const slots: Readonly<Slots<T>> & Slots<T> = null!
  const s = slots['slot-bar']
  const s2: Slots<T> = null!
  const s3 = s2['slot-bar']
}
