// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "esnext", "module": "esnext", "lib": ["esnext"], "types": [], "moduleDetection": "force"}, "files": ["element_types.ts", "loop__do_while__for_of_const__none.ts", "loop__do_while__for_of_property__break.ts", "loop__do_while__for_of_property__continue.ts", "loop__do_while__for_of_property__none.ts", "loop__for__for_of_const__none.ts", "loop__for__for_of_copy__break.ts", "loop__for__for_of_copy__continue.ts", "loop__for__for_of_copy__none.ts", "loop__for_of_outer__for_of_const__none.ts", "loop__in_function.ts", "loop__labelled_for__for_of_const__none.ts", "loop__labelled_while__annotated__labelled_continue.ts", "loop__labelled_while__annotated__none.ts", "loop__labelled_while__array_pattern__labelled_continue.ts", "loop__labelled_while__array_pattern__none.ts", "loop__labelled_while__classic_for__labelled_continue.ts", "loop__labelled_while__classic_for__none.ts", "loop__labelled_while__for_in__labelled_continue.ts", "loop__labelled_while__for_in__none.ts", "loop__labelled_while__for_in_key__labelled_continue.ts", "loop__labelled_while__for_in_key__none.ts", "loop__labelled_while__for_of_array_pattern__labelled_continue.ts", "loop__labelled_while__for_of_array_pattern__none.ts", "loop__labelled_while__for_of_assigned_after__none.ts", "loop__labelled_while__for_of_const__labelled_continue.ts", "loop__labelled_while__for_of_const__none.ts", "loop__labelled_while__for_of_copy__labelled_continue.ts", "loop__labelled_while__for_of_copy__none.ts", "loop__labelled_while__for_of_declared_before__labelled_continue.ts", "loop__labelled_while__for_of_declared_before__none.ts", "loop__labelled_while__for_of_let__labelled_continue.ts", "loop__labelled_while__for_of_let__none.ts", "loop__labelled_while__for_of_pattern__labelled_continue.ts", "loop__labelled_while__for_of_pattern__none.ts", "loop__labelled_while__for_of_property__labelled_continue.ts", "loop__labelled_while__for_of_property__none.ts", "loop__labelled_while__function_between__labelled_continue.ts", "loop__labelled_while__function_between__none.ts", "loop__labelled_while__indexed__labelled_continue.ts", "loop__labelled_while__indexed__none.ts", "loop__labelled_while__inner_while__labelled_continue.ts", "loop__labelled_while__inner_while__none.ts", "loop__labelled_while__no_pattern__labelled_continue.ts", "loop__labelled_while__no_pattern__none.ts", "loop__while__for_of_const__none.ts", "nested_defaults.ts", "pattern__any__sibling_with_default.ts", "pattern__declared__each_other.ts", "pattern__empty__sibling.ts", "pattern__expect_error.ts", "pattern__tuple__array_with_default.ts", "pattern__union__nested_with_default.ts", "pattern__unknown__array.ts", "pattern__unknown__array_with_default.ts", "pattern__unknown__each_other.ts", "pattern__unknown__function_default.ts", "pattern__unknown__nested.ts", "pattern__unknown__nested_with_default.ts", "pattern__unknown__no_default.ts", "pattern__unknown__renamed.ts", "pattern__unknown__rest.ts", "pattern__unknown__sibling.ts", "pattern__unknown__sibling_with_default.ts", "reported_expect_error.ts", "reported_loop.ts", "reported_parameter.ts"]}
// @filename: element_types.ts
export {}
interface Sink { take(value: unknown): void }
const p1: Sink = { take({ first = 1 }) { const p: never = first } }
const p2: Sink = { take({ first = 1, second = 2 }) { const p: never = first } }
const p3: Sink = { take({ first = 1, second = first }) { const p: never = first } }
const p4: Sink = { take({ first = 1, second = first }) { const q: never = second } }
const p5: Sink = { take({ first = 1, second = () => first }) { const p: never = first } }
const p6: Sink = { take(value = { first: 1 }) { const p: never = value } }
// @filename: loop__do_while__for_of_const__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
do {
  if (!current) break
  const { children, parent } = current
  for (const child of children) { if (child.size > 1) { current = child;  } }
  current = parent
} while (current)
export const sentinel: number = ""
// @filename: loop__do_while__for_of_property__break.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
do {
  if (!current) break
  for (const child of current.children) { current = child; break }
} while (current)
export const sentinel: number = ""
// @filename: loop__do_while__for_of_property__continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
do {
  if (!current) break
  for (const child of current.children) { current = child; continue }
} while (current)
export const sentinel: number = ""
// @filename: loop__do_while__for_of_property__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
do {
  if (!current) break
  for (const child of current.children) { current = child;  }
} while (current)
export const sentinel: number = ""
// @filename: loop__for__for_of_const__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
for (;;) {
  if (!current) break
  const { children, parent } = current
  for (const child of children) { if (child.size > 1) { current = child;  } }
  current = parent
}
export const sentinel: number = ""
// @filename: loop__for__for_of_copy__break.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
for (;;) {
  if (!current) break
  const { children } = current
  for (const child of children) { const copy = child; current = copy; break }
}
export const sentinel: number = ""
// @filename: loop__for__for_of_copy__continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
for (;;) {
  if (!current) break
  const { children } = current
  for (const child of children) { const copy = child; current = copy; continue }
}
export const sentinel: number = ""
// @filename: loop__for__for_of_copy__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
for (;;) {
  if (!current) break
  const { children } = current
  for (const child of children) { const copy = child; current = copy;  }
}
export const sentinel: number = ""
// @filename: loop__for_of_outer__for_of_const__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
for (const turn of [1, 2, 3]) {
  if (!current) break
  const { children, parent } = current
  for (const child of children) { if (child.size > 1) { current = child;  } }
  current = parent
}
export const sentinel: number = ""
// @filename: loop__in_function.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
function walk(from: Item) {
  let current: Item | undefined = from
  while (current) {
    const { children } = current
    for (const child of children) { current = child }
  }
}
async function walkAsync(from: Item) {
  let current: Item | undefined = from
  while (current) {
    for await (const child of items) { current = child.size ? child : current.first }
    const { first } = current
    for await (const child of [first]) { current = child }
  }
}
export const sentinel: number = ""
// @filename: loop__labelled_for__for_of_const__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: for (; current; ) {
  const { children, parent } = current
  for (const child of children) { if (child.size > 1) { current = child;  } }
  current = parent
}
export const sentinel: number = ""
// @filename: loop__labelled_while__annotated__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const children: Item[] = current.children
  for (const child of children) { current = child; continue outer }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__annotated__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const children: Item[] = current.children
  for (const child of children) { current = child;  }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__array_pattern__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const [head] = current.children
  for (const child of head.children) { current = child; continue outer }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__array_pattern__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const [head] = current.children
  for (const child of head.children) { current = child;  }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__classic_for__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  for (let i = 0; i < children.length; i++) { const child = children[i]; current = child; continue outer }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__classic_for__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  for (let i = 0; i < children.length; i++) { const child = children[i]; current = child;  }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_in__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { byName } = current
  for (const name in byName) { current = byName[name]; continue outer }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_in__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { byName } = current
  for (const name in byName) { current = byName[name];  }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_in_key__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { byName } = current
  let key: string | undefined
  for (const name in byName) { key = name; continue outer }
  current = key ? byName[key] : undefined
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_in_key__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { byName } = current
  let key: string | undefined
  for (const name in byName) { key = name;  }
  current = key ? byName[key] : undefined
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_array_pattern__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  for (const [index, child] of children.entries()) { current = child; continue outer }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_array_pattern__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  for (const [index, child] of children.entries()) { current = child;  }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_assigned_after__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  let last: Item | undefined
  for (const child of children) { last = child }
  current = last
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_const__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children, parent } = current
  for (const child of children) { if (child.size > 1) { current = child; continue outer } }
  current = parent
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_const__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children, parent } = current
  for (const child of children) { if (child.size > 1) { current = child;  } }
  current = parent
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_copy__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  for (const child of children) { const copy = child; current = copy; continue outer }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_copy__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  for (const child of children) { const copy = child; current = copy;  }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_declared_before__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  let child: Item
  for (child of children) { current = child; continue outer }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_declared_before__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  let child: Item
  for (child of children) { current = child;  }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_let__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  for (let child of children) { current = child; continue outer }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_let__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  for (let child of children) { current = child;  }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_pattern__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  for (const { first } of children) { current = first; continue outer }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_pattern__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  for (const { first } of children) { current = first;  }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_property__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  for (const child of current.children) { current = child; continue outer }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__for_of_property__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  for (const child of current.children) { current = child;  }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__function_between__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  for (const child of children) { const set = () => { current = child }; set(); continue outer }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__function_between__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children } = current
  for (const child of children) { const set = () => { current = child }; set();  }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__indexed__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children, parent } = current
  const child = children[0]
  if (child.size > 1) { current = child; continue outer }
  current = parent
}
export const sentinel: number = ""
// @filename: loop__labelled_while__indexed__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { children, parent } = current
  const child = children[0]
  if (child.size > 1) { current = child;  }
  current = parent
}
export const sentinel: number = ""
// @filename: loop__labelled_while__inner_while__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { first, parent } = current
  while (first.size > 1) { current = first; continue outer }
  current = parent
}
export const sentinel: number = ""
// @filename: loop__labelled_while__inner_while__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const { first, parent } = current
  while (first.size > 1) { current = first;  }
  current = parent
}
export const sentinel: number = ""
// @filename: loop__labelled_while__no_pattern__labelled_continue.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const children = current.children
  for (const child of children) { current = child; continue outer }
}
export const sentinel: number = ""
// @filename: loop__labelled_while__no_pattern__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
outer: while (current) {
  const children = current.children
  for (const child of children) { current = child;  }
}
export const sentinel: number = ""
// @filename: loop__while__for_of_const__none.ts
export {}
interface Item { parent?: Item; children: Item[]; size: number; first: Item; byName: Record<string, Item> }
declare const start: Item
declare const items: AsyncIterable<Item>
let current: Item | undefined = start
while (current) {
  const { children, parent } = current
  for (const child of children) { if (child.size > 1) { current = child;  } }
  current = parent
}
export const sentinel: number = ""
// @filename: nested_defaults.ts
export {}
interface Sink { take(value: unknown): void }
const n1: Sink = { take({ first: { deep = '' } = {} }) { const p: never = deep } }
const n2: Sink = { take({ first: { deep = '', other = 1 } = {} }) { const p: never = deep } }
const n3: Sink = { take({ first: { deep = '', other = deep } = {} }) { const p: never = deep } }
const n4: Sink = { take({ first: { deep = '', other = deep } = {} }) { const q: never = other } }
const n5: Sink = { take({ first: { deep = '', other = deep } }) { const p: never = deep } }
const n6: Sink = { take({ first: { deep = '', other = deep } = { deep: 'x' } }) { const p: never = deep } }
const n7: Sink = { take({ first: [deep = '', other = deep] = [] }) { const p: never = deep } }
const n8: Sink = { take({ first = {}, second = first }) { const p: never = first } }
const n9: Sink = { take(value = {}) { const p: never = value } }
// @filename: pattern__any__sibling_with_default.ts
export {}
interface Sink { take(value: any): void }
declare function call(f: (value: any) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take({ first = 1, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
const arrow: Sink = { take: ({ first = 1, second = first }) => { const probe_first: never = first; const probe_second: never = second; } }
const expression: Sink = { take: function ({ first = 1, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
call(({ first = 1, second = first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred = generic(({ first = 1, second = first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred_probe: never = inferred
class Base { take(value: any) {} }
class Derived extends Base { take({ first = 1, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
function declared({ first = 1, second = first }: any) { const probe_first: never = first; const probe_second: never = second; }
const second_parameter: (a: any, b: any) => void = ({ first = 1, second = first }, other = 1) => { const probe_first: never = first; const probe_second: never = second; }
export const sentinel: number = ""
// @filename: pattern__declared__each_other.ts
export {}
interface Sink { take(value: { first: number; second?: number }): void }
declare function call(f: (value: { first: number; second?: number }) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take({ first = second, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
const arrow: Sink = { take: ({ first = second, second = first }) => { const probe_first: never = first; const probe_second: never = second; } }
const expression: Sink = { take: function ({ first = second, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
call(({ first = second, second = first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred = generic(({ first = second, second = first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred_probe: never = inferred
class Base { take(value: { first: number; second?: number }) {} }
class Derived extends Base { take({ first = second, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
function declared({ first = second, second = first }: { first: number; second?: number }) { const probe_first: never = first; const probe_second: never = second; }
const second_parameter: (a: { first: number; second?: number }, b: { first: number; second?: number }) => void = ({ first = second, second = first }, other = 1) => { const probe_first: never = first; const probe_second: never = second; }
export const sentinel: number = ""
// @filename: pattern__empty__sibling.ts
export {}
interface Sink { take(value: {}): void }
declare function call(f: (value: {}) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take({ first, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
const arrow: Sink = { take: ({ first, second = first }) => { const probe_first: never = first; const probe_second: never = second; } }
const expression: Sink = { take: function ({ first, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
call(({ first, second = first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred = generic(({ first, second = first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred_probe: never = inferred
class Base { take(value: {}) {} }
class Derived extends Base { take({ first, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
function declared({ first, second = first }: {}) { const probe_first: never = first; const probe_second: never = second; }
const second_parameter: (a: {}, b: {}) => void = ({ first, second = first }, other = 1) => { const probe_first: never = first; const probe_second: never = second; }
export const sentinel: number = ""
// @filename: pattern__expect_error.ts
export {}
interface Sink { take(value: unknown): void }
declare function call(f: (value: unknown) => void): void
declare function generic<T>(f: (value: T) => void): T
const sink: Sink = {
  take({
    // @ts-expect-error "outer" is not on unknown
    outer: {
      first,
      second = first,
    },
  }) {
    void second
  },
}
export const sentinel: number = ""
// @filename: pattern__tuple__array_with_default.ts
export {}
interface Sink { take(value: [number, number?]): void }
declare function call(f: (value: [number, number?]) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take([first = 1, second = first]) { const probe_first: never = first; const probe_second: never = second; } }
const arrow: Sink = { take: ([first = 1, second = first]) => { const probe_first: never = first; const probe_second: never = second; } }
const expression: Sink = { take: function ([first = 1, second = first]) { const probe_first: never = first; const probe_second: never = second; } }
call(([first = 1, second = first]) => { const probe_first: never = first; const probe_second: never = second; })
const inferred = generic(([first = 1, second = first]) => { const probe_first: never = first; const probe_second: never = second; })
const inferred_probe: never = inferred
class Base { take(value: [number, number?]) {} }
class Derived extends Base { take([first = 1, second = first]) { const probe_first: never = first; const probe_second: never = second; } }
function declared([first = 1, second = first]: [number, number?]) { const probe_first: never = first; const probe_second: never = second; }
const second_parameter: (a: [number, number?], b: [number, number?]) => void = ([first = 1, second = first], other = 1) => { const probe_first: never = first; const probe_second: never = second; }
export const sentinel: number = ""
// @filename: pattern__union__nested_with_default.ts
export {}
interface Sink { take(value: { first: 1 } | { first: 2 }): void }
declare function call(f: (value: { first: 1 } | { first: 2 }) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take({ first: { deep = '', other = deep } = {} }) { const probe_deep: never = deep; const probe_other: never = other; } }
const arrow: Sink = { take: ({ first: { deep = '', other = deep } = {} }) => { const probe_deep: never = deep; const probe_other: never = other; } }
const expression: Sink = { take: function ({ first: { deep = '', other = deep } = {} }) { const probe_deep: never = deep; const probe_other: never = other; } }
call(({ first: { deep = '', other = deep } = {} }) => { const probe_deep: never = deep; const probe_other: never = other; })
const inferred = generic(({ first: { deep = '', other = deep } = {} }) => { const probe_deep: never = deep; const probe_other: never = other; })
const inferred_probe: never = inferred
class Base { take(value: { first: 1 } | { first: 2 }) {} }
class Derived extends Base { take({ first: { deep = '', other = deep } = {} }) { const probe_deep: never = deep; const probe_other: never = other; } }
function declared({ first: { deep = '', other = deep } = {} }: { first: 1 } | { first: 2 }) { const probe_deep: never = deep; const probe_other: never = other; }
const second_parameter: (a: { first: 1 } | { first: 2 }, b: { first: 1 } | { first: 2 }) => void = ({ first: { deep = '', other = deep } = {} }, other = 1) => { const probe_deep: never = deep; const probe_other: never = other; }
export const sentinel: number = ""
// @filename: pattern__unknown__array.ts
export {}
interface Sink { take(value: unknown): void }
declare function call(f: (value: unknown) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take([first, second = first]) { const probe_first: never = first; const probe_second: never = second; } }
const arrow: Sink = { take: ([first, second = first]) => { const probe_first: never = first; const probe_second: never = second; } }
const expression: Sink = { take: function ([first, second = first]) { const probe_first: never = first; const probe_second: never = second; } }
call(([first, second = first]) => { const probe_first: never = first; const probe_second: never = second; })
const inferred = generic(([first, second = first]) => { const probe_first: never = first; const probe_second: never = second; })
const inferred_probe: never = inferred
class Base { take(value: unknown) {} }
class Derived extends Base { take([first, second = first]) { const probe_first: never = first; const probe_second: never = second; } }
function declared([first, second = first]: unknown) { const probe_first: never = first; const probe_second: never = second; }
const second_parameter: (a: unknown, b: unknown) => void = ([first, second = first], other = 1) => { const probe_first: never = first; const probe_second: never = second; }
export const sentinel: number = ""
// @filename: pattern__unknown__array_with_default.ts
export {}
interface Sink { take(value: unknown): void }
declare function call(f: (value: unknown) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take([first = 1, second = first]) { const probe_first: never = first; const probe_second: never = second; } }
const arrow: Sink = { take: ([first = 1, second = first]) => { const probe_first: never = first; const probe_second: never = second; } }
const expression: Sink = { take: function ([first = 1, second = first]) { const probe_first: never = first; const probe_second: never = second; } }
call(([first = 1, second = first]) => { const probe_first: never = first; const probe_second: never = second; })
const inferred = generic(([first = 1, second = first]) => { const probe_first: never = first; const probe_second: never = second; })
const inferred_probe: never = inferred
class Base { take(value: unknown) {} }
class Derived extends Base { take([first = 1, second = first]) { const probe_first: never = first; const probe_second: never = second; } }
function declared([first = 1, second = first]: unknown) { const probe_first: never = first; const probe_second: never = second; }
const second_parameter: (a: unknown, b: unknown) => void = ([first = 1, second = first], other = 1) => { const probe_first: never = first; const probe_second: never = second; }
export const sentinel: number = ""
// @filename: pattern__unknown__each_other.ts
export {}
interface Sink { take(value: unknown): void }
declare function call(f: (value: unknown) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take({ first = second, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
const arrow: Sink = { take: ({ first = second, second = first }) => { const probe_first: never = first; const probe_second: never = second; } }
const expression: Sink = { take: function ({ first = second, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
call(({ first = second, second = first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred = generic(({ first = second, second = first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred_probe: never = inferred
class Base { take(value: unknown) {} }
class Derived extends Base { take({ first = second, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
function declared({ first = second, second = first }: unknown) { const probe_first: never = first; const probe_second: never = second; }
const second_parameter: (a: unknown, b: unknown) => void = ({ first = second, second = first }, other = 1) => { const probe_first: never = first; const probe_second: never = second; }
export const sentinel: number = ""
// @filename: pattern__unknown__function_default.ts
export {}
interface Sink { take(value: unknown): void }
declare function call(f: (value: unknown) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take({ first = 1, second = () => first }) { const probe_first: never = first; const probe_second: never = second; } }
const arrow: Sink = { take: ({ first = 1, second = () => first }) => { const probe_first: never = first; const probe_second: never = second; } }
const expression: Sink = { take: function ({ first = 1, second = () => first }) { const probe_first: never = first; const probe_second: never = second; } }
call(({ first = 1, second = () => first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred = generic(({ first = 1, second = () => first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred_probe: never = inferred
class Base { take(value: unknown) {} }
class Derived extends Base { take({ first = 1, second = () => first }) { const probe_first: never = first; const probe_second: never = second; } }
function declared({ first = 1, second = () => first }: unknown) { const probe_first: never = first; const probe_second: never = second; }
const second_parameter: (a: unknown, b: unknown) => void = ({ first = 1, second = () => first }, other = 1) => { const probe_first: never = first; const probe_second: never = second; }
export const sentinel: number = ""
// @filename: pattern__unknown__nested.ts
export {}
interface Sink { take(value: unknown): void }
declare function call(f: (value: unknown) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take({ first: { deep, other = deep } }) { const probe_deep: never = deep; const probe_other: never = other; } }
const arrow: Sink = { take: ({ first: { deep, other = deep } }) => { const probe_deep: never = deep; const probe_other: never = other; } }
const expression: Sink = { take: function ({ first: { deep, other = deep } }) { const probe_deep: never = deep; const probe_other: never = other; } }
call(({ first: { deep, other = deep } }) => { const probe_deep: never = deep; const probe_other: never = other; })
const inferred = generic(({ first: { deep, other = deep } }) => { const probe_deep: never = deep; const probe_other: never = other; })
const inferred_probe: never = inferred
class Base { take(value: unknown) {} }
class Derived extends Base { take({ first: { deep, other = deep } }) { const probe_deep: never = deep; const probe_other: never = other; } }
function declared({ first: { deep, other = deep } }: unknown) { const probe_deep: never = deep; const probe_other: never = other; }
const second_parameter: (a: unknown, b: unknown) => void = ({ first: { deep, other = deep } }, other = 1) => { const probe_deep: never = deep; const probe_other: never = other; }
export const sentinel: number = ""
// @filename: pattern__unknown__nested_with_default.ts
export {}
interface Sink { take(value: unknown): void }
declare function call(f: (value: unknown) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take({ first: { deep = '', other = deep } = {} }) { const probe_deep: never = deep; const probe_other: never = other; } }
const arrow: Sink = { take: ({ first: { deep = '', other = deep } = {} }) => { const probe_deep: never = deep; const probe_other: never = other; } }
const expression: Sink = { take: function ({ first: { deep = '', other = deep } = {} }) { const probe_deep: never = deep; const probe_other: never = other; } }
call(({ first: { deep = '', other = deep } = {} }) => { const probe_deep: never = deep; const probe_other: never = other; })
const inferred = generic(({ first: { deep = '', other = deep } = {} }) => { const probe_deep: never = deep; const probe_other: never = other; })
const inferred_probe: never = inferred
class Base { take(value: unknown) {} }
class Derived extends Base { take({ first: { deep = '', other = deep } = {} }) { const probe_deep: never = deep; const probe_other: never = other; } }
function declared({ first: { deep = '', other = deep } = {} }: unknown) { const probe_deep: never = deep; const probe_other: never = other; }
const second_parameter: (a: unknown, b: unknown) => void = ({ first: { deep = '', other = deep } = {} }, other = 1) => { const probe_deep: never = deep; const probe_other: never = other; }
export const sentinel: number = ""
// @filename: pattern__unknown__no_default.ts
export {}
interface Sink { take(value: unknown): void }
declare function call(f: (value: unknown) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take({ first, second }) { const probe_first: never = first; const probe_second: never = second; } }
const arrow: Sink = { take: ({ first, second }) => { const probe_first: never = first; const probe_second: never = second; } }
const expression: Sink = { take: function ({ first, second }) { const probe_first: never = first; const probe_second: never = second; } }
call(({ first, second }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred = generic(({ first, second }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred_probe: never = inferred
class Base { take(value: unknown) {} }
class Derived extends Base { take({ first, second }) { const probe_first: never = first; const probe_second: never = second; } }
function declared({ first, second }: unknown) { const probe_first: never = first; const probe_second: never = second; }
const second_parameter: (a: unknown, b: unknown) => void = ({ first, second }, other = 1) => { const probe_first: never = first; const probe_second: never = second; }
export const sentinel: number = ""
// @filename: pattern__unknown__renamed.ts
export {}
interface Sink { take(value: unknown): void }
declare function call(f: (value: unknown) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take({ first: one, second: two = one }) { const probe_one: never = one; const probe_second: never = second; const probe_two: never = two; } }
const arrow: Sink = { take: ({ first: one, second: two = one }) => { const probe_one: never = one; const probe_second: never = second; const probe_two: never = two; } }
const expression: Sink = { take: function ({ first: one, second: two = one }) { const probe_one: never = one; const probe_second: never = second; const probe_two: never = two; } }
call(({ first: one, second: two = one }) => { const probe_one: never = one; const probe_second: never = second; const probe_two: never = two; })
const inferred = generic(({ first: one, second: two = one }) => { const probe_one: never = one; const probe_second: never = second; const probe_two: never = two; })
const inferred_probe: never = inferred
class Base { take(value: unknown) {} }
class Derived extends Base { take({ first: one, second: two = one }) { const probe_one: never = one; const probe_second: never = second; const probe_two: never = two; } }
function declared({ first: one, second: two = one }: unknown) { const probe_one: never = one; const probe_second: never = second; const probe_two: never = two; }
const second_parameter: (a: unknown, b: unknown) => void = ({ first: one, second: two = one }, other = 1) => { const probe_one: never = one; const probe_second: never = second; const probe_two: never = two; }
export const sentinel: number = ""
// @filename: pattern__unknown__rest.ts
export {}
interface Sink { take(value: unknown): void }
declare function call(f: (value: unknown) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take({ first, ...others }) { const probe_first: never = first; const probe_others: never = others; } }
const arrow: Sink = { take: ({ first, ...others }) => { const probe_first: never = first; const probe_others: never = others; } }
const expression: Sink = { take: function ({ first, ...others }) { const probe_first: never = first; const probe_others: never = others; } }
call(({ first, ...others }) => { const probe_first: never = first; const probe_others: never = others; })
const inferred = generic(({ first, ...others }) => { const probe_first: never = first; const probe_others: never = others; })
const inferred_probe: never = inferred
class Base { take(value: unknown) {} }
class Derived extends Base { take({ first, ...others }) { const probe_first: never = first; const probe_others: never = others; } }
function declared({ first, ...others }: unknown) { const probe_first: never = first; const probe_others: never = others; }
const second_parameter: (a: unknown, b: unknown) => void = ({ first, ...others }, other = 1) => { const probe_first: never = first; const probe_others: never = others; }
export const sentinel: number = ""
// @filename: pattern__unknown__sibling.ts
export {}
interface Sink { take(value: unknown): void }
declare function call(f: (value: unknown) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take({ first, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
const arrow: Sink = { take: ({ first, second = first }) => { const probe_first: never = first; const probe_second: never = second; } }
const expression: Sink = { take: function ({ first, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
call(({ first, second = first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred = generic(({ first, second = first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred_probe: never = inferred
class Base { take(value: unknown) {} }
class Derived extends Base { take({ first, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
function declared({ first, second = first }: unknown) { const probe_first: never = first; const probe_second: never = second; }
const second_parameter: (a: unknown, b: unknown) => void = ({ first, second = first }, other = 1) => { const probe_first: never = first; const probe_second: never = second; }
export const sentinel: number = ""
// @filename: pattern__unknown__sibling_with_default.ts
export {}
interface Sink { take(value: unknown): void }
declare function call(f: (value: unknown) => void): void
declare function generic<T>(f: (value: T) => void): T
const method: Sink = { take({ first = 1, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
const arrow: Sink = { take: ({ first = 1, second = first }) => { const probe_first: never = first; const probe_second: never = second; } }
const expression: Sink = { take: function ({ first = 1, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
call(({ first = 1, second = first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred = generic(({ first = 1, second = first }) => { const probe_first: never = first; const probe_second: never = second; })
const inferred_probe: never = inferred
class Base { take(value: unknown) {} }
class Derived extends Base { take({ first = 1, second = first }) { const probe_first: never = first; const probe_second: never = second; } }
function declared({ first = 1, second = first }: unknown) { const probe_first: never = first; const probe_second: never = second; }
const second_parameter: (a: unknown, b: unknown) => void = ({ first = 1, second = first }, other = 1) => { const probe_first: never = first; const probe_second: never = second; }
export const sentinel: number = ""
// @filename: reported_expect_error.ts
export {}
interface Sink { take(value: unknown): void }
const sink: Sink = {
  take({
    // @ts-expect-error "outer" is not on unknown
    outer: {
      first,
      second = first,
    },
  }) {
    void second
  },
}
void sink
// @filename: reported_loop.ts
export {}
interface Node { parent?: Node; children: Node[]; size: number }
declare const start: Node
let current: Node | undefined = start
outer: while (current) {
  const { children, parent } = current
  for (const child of children) {
    if (child.size > 1) {
      current = child
      continue outer
    }
  }
  current = parent
}
// @filename: reported_parameter.ts
export {}
interface Sink { take(value: unknown): void }
const sink: Sink = { take({ first, second = first }) { void second } }
void sink
