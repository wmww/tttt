// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "esnext", "module": "esnext", "lib": ["esnext"], "types": [], "moduleDetection": "force"}, "files": ["accessor_in_a_loop.ts", "literal_properties_in_a_circle.ts", "rest_in_an_array_pattern.ts", "iterators_without_a_value.ts", "iterators_without_a_value_each_its_own_type.ts"]}
// @filename: accessor_in_a_loop.ts
export {}
interface Item { children: Item[]; first: Item }
declare const start: Item
let current: Item | undefined = start
while (current) {
  const { children } = current
  class Holder { accessor held = children[0] }
  current = new Holder().held
}
let other: Item | undefined = start
while (other) {
  const { first } = other
  class Fixed { static accessor kept = first }
  other = Fixed.kept
}
let third: Item | undefined = start
while (third) {
  const { first } = third
  class Typed { accessor kept: Item = first }
  third = new Typed().kept
}
let fourth: Item | undefined = start
while (fourth) {
  const { first } = fourth
  const holder = { get kept() { return first }, set kept(value) {} }
  fourth = holder.kept
}
export const sentinel: number = ''
// @filename: iterators_without_a_value.ts
export {}
declare const bad: { [Symbol.iterator](): { next(): { done: boolean } } }
declare const worse: { [Symbol.iterator](): { next(): number } }
declare const good: Iterable<string | number>
// read in a closure first, so the flow of the pattern is resolved before the declaration is checked
function early() { return [first1, second1] }
const [first1, second1] = bad
const [a2, b2 = a2] = bad
let [a3] = bad
if (typeof a3 === 'string') { const f = () => a3 }
while (Math.random() < 0.5) {
  const [a4, b4] = bad
  const probe: never = a4
  const [c4] = worse
}
for (const [a5] of [bad]) { const probe: never = a5 }
function params([a6, b6]: typeof bad, [c6] = bad) { return a6 ?? b6 ?? c6 }
let a7, b7
;[a7, b7] = bad
;[a7 = 1, ...b7] = bad
const [[a8]] = [bad]
const { nested: [a9] } = { nested: bad }
for (const x10 of bad) { const probe: never = x10 }
const spread = [...bad]
const [a11] = good
let current: string | number = 0
while (Math.random() < 0.5) {
  const [a12] = bad
  const [b12] = [current]
  current = typeof b12 === 'string' ? 1 : (a12 as string)
}
async function gen() { for await (const [a13] of [bad]) { void a13 } }
export const sentinel: number = ''
// @filename: iterators_without_a_value_each_its_own_type.ts
export {}
namespace N1 {
  declare const bad: { tag: 1; [Symbol.iterator](): { next(): { done: boolean } } }
  function early() { return [x, y] }
  const [x, y] = bad
}
namespace N2 {
  declare const bad: { tag: 2; [Symbol.iterator](): { next(): { done: boolean } } }
  const [x, y = x] = bad
}
namespace N3 {
  declare const bad: { tag: 3; [Symbol.iterator](): { next(): { done: boolean } } }
  let [x] = bad
  if (typeof x === 'string') { const f = () => x }
}
namespace N4 {
  declare const bad: { tag: 4; [Symbol.iterator](): { next(): { done: boolean } } }
  while (Math.random() < 0.5) { const [x, y] = bad; const probe: never = x }
}
namespace N5 {
  declare const bad: { tag: 5; [Symbol.iterator](): { next(): { done: boolean } } }
  for (const [x] of [bad]) { const probe: never = x }
}
namespace N6 {
  declare const bad: { tag: 6; [Symbol.iterator](): { next(): { done: boolean } } }
  function params([x, y]: typeof bad) { return x ?? y }
}
namespace N7 {
  declare const bad: { tag: 7; [Symbol.iterator](): { next(): { done: boolean } } }
  function defaults([x] = bad) { return x }
}
namespace N8 {
  declare const bad: { tag: 8; [Symbol.iterator](): { next(): { done: boolean } } }
  let x, y
  ;[x, y] = bad
}
namespace N9 {
  declare const bad: { tag: 9; [Symbol.iterator](): { next(): { done: boolean } } }
  let x, y
  ;[x = 1, ...y] = bad
}
namespace N10 {
  declare const bad: { tag: 10; [Symbol.iterator](): { next(): { done: boolean } } }
  const [[x]] = [bad]
}
namespace N11 {
  declare const bad: { tag: 11; [Symbol.iterator](): { next(): { done: boolean } } }
  const { nested: [x] } = { nested: bad }
}
namespace N12 {
  declare const bad: { tag: 12; [Symbol.iterator](): { next(): { done: boolean } } }
  for (const x of bad) { const probe: never = x }
}
namespace N13 {
  declare const bad: { tag: 13; [Symbol.iterator](): { next(): { done: boolean } } }
  const spread = [...bad]
}
namespace N14 {
  declare const bad: { tag: 14; [Symbol.iterator](): { next(): { done: boolean } } }
  let current: string | number = 0
  while (Math.random() < 0.5) { const [x] = bad; const [y] = [current]; current = typeof y === 'string' ? 1 : (x as string) }
}
namespace N15 {
  declare const bad: { tag: 15; [Symbol.iterator](): { next(): { done: boolean } } }
  let current: unknown = 0
  while (current) { const [x] = bad; current = x }
}
namespace N16 {
  declare const bad: { tag: 16; [Symbol.iterator](): { next(): { done: boolean } } }
  let current: unknown = 0
  while (current) { const [x, y = x] = bad; for (const z of bad) { current = z ?? y } }
}
namespace N17 {
  declare const bad: { tag: 17; [Symbol.iterator](): { next(): { done: boolean } } }
  const f = () => { const [x] = bad; return x }
  const [y] = bad
}
namespace N18 {
  declare const bad: { tag: 18; [Symbol.iterator](): { next(): { done: boolean } } }
  function* g() { yield* bad }
}
namespace N19 {
  declare const bad: { tag: 19; [Symbol.iterator](): { next(): { done: boolean } } }
  const [x] = bad
  const [y] = bad
}
namespace N20 {
  declare const bad: { tag: 20; [Symbol.iterator](): { next(): { done: boolean } } }
  let x
  while (Math.random() < 0.5) { [x] = bad }
}
namespace N21 {
  declare const bad: { tag: 21; [Symbol.iterator](): { next(): number } }
  function early() { return [x, y] }
  const [x, y] = bad
}
namespace N22 {
  declare const bad: { tag: 22; [Symbol.iterator](): { next(): number } }
  const [x, y = x] = bad
}
namespace N23 {
  declare const bad: { tag: 23; [Symbol.iterator](): { next(): number } }
  let [x] = bad
  if (typeof x === 'string') { const f = () => x }
}
namespace N24 {
  declare const bad: { tag: 24; [Symbol.iterator](): { next(): number } }
  while (Math.random() < 0.5) { const [x, y] = bad; const probe: never = x }
}
namespace N25 {
  declare const bad: { tag: 25; [Symbol.iterator](): { next(): number } }
  for (const [x] of [bad]) { const probe: never = x }
}
namespace N26 {
  declare const bad: { tag: 26; [Symbol.iterator](): { next(): number } }
  function params([x, y]: typeof bad) { return x ?? y }
}
namespace N27 {
  declare const bad: { tag: 27; [Symbol.iterator](): { next(): number } }
  function defaults([x] = bad) { return x }
}
namespace N28 {
  declare const bad: { tag: 28; [Symbol.iterator](): { next(): number } }
  let x, y
  ;[x, y] = bad
}
namespace N29 {
  declare const bad: { tag: 29; [Symbol.iterator](): { next(): number } }
  let x, y
  ;[x = 1, ...y] = bad
}
namespace N30 {
  declare const bad: { tag: 30; [Symbol.iterator](): { next(): number } }
  const [[x]] = [bad]
}
namespace N31 {
  declare const bad: { tag: 31; [Symbol.iterator](): { next(): number } }
  const { nested: [x] } = { nested: bad }
}
namespace N32 {
  declare const bad: { tag: 32; [Symbol.iterator](): { next(): number } }
  for (const x of bad) { const probe: never = x }
}
namespace N33 {
  declare const bad: { tag: 33; [Symbol.iterator](): { next(): number } }
  const spread = [...bad]
}
namespace N34 {
  declare const bad: { tag: 34; [Symbol.iterator](): { next(): number } }
  let current: string | number = 0
  while (Math.random() < 0.5) { const [x] = bad; const [y] = [current]; current = typeof y === 'string' ? 1 : (x as string) }
}
namespace N35 {
  declare const bad: { tag: 35; [Symbol.iterator](): { next(): number } }
  let current: unknown = 0
  while (current) { const [x] = bad; current = x }
}
namespace N36 {
  declare const bad: { tag: 36; [Symbol.iterator](): { next(): number } }
  let current: unknown = 0
  while (current) { const [x, y = x] = bad; for (const z of bad) { current = z ?? y } }
}
namespace N37 {
  declare const bad: { tag: 37; [Symbol.iterator](): { next(): number } }
  const f = () => { const [x] = bad; return x }
  const [y] = bad
}
namespace N38 {
  declare const bad: { tag: 38; [Symbol.iterator](): { next(): number } }
  function* g() { yield* bad }
}
namespace N39 {
  declare const bad: { tag: 39; [Symbol.iterator](): { next(): number } }
  const [x] = bad
  const [y] = bad
}
namespace N40 {
  declare const bad: { tag: 40; [Symbol.iterator](): { next(): number } }
  let x
  while (Math.random() < 0.5) { [x] = bad }
}
namespace N41 {
  declare const bad: { tag: 41; [Symbol.iterator](): { next: number } }
  function early() { return [x, y] }
  const [x, y] = bad
}
namespace N42 {
  declare const bad: { tag: 42; [Symbol.iterator](): { next: number } }
  const [x, y = x] = bad
}
namespace N43 {
  declare const bad: { tag: 43; [Symbol.iterator](): { next: number } }
  let [x] = bad
  if (typeof x === 'string') { const f = () => x }
}
namespace N44 {
  declare const bad: { tag: 44; [Symbol.iterator](): { next: number } }
  while (Math.random() < 0.5) { const [x, y] = bad; const probe: never = x }
}
namespace N45 {
  declare const bad: { tag: 45; [Symbol.iterator](): { next: number } }
  for (const [x] of [bad]) { const probe: never = x }
}
namespace N46 {
  declare const bad: { tag: 46; [Symbol.iterator](): { next: number } }
  function params([x, y]: typeof bad) { return x ?? y }
}
namespace N47 {
  declare const bad: { tag: 47; [Symbol.iterator](): { next: number } }
  function defaults([x] = bad) { return x }
}
namespace N48 {
  declare const bad: { tag: 48; [Symbol.iterator](): { next: number } }
  let x, y
  ;[x, y] = bad
}
namespace N49 {
  declare const bad: { tag: 49; [Symbol.iterator](): { next: number } }
  let x, y
  ;[x = 1, ...y] = bad
}
namespace N50 {
  declare const bad: { tag: 50; [Symbol.iterator](): { next: number } }
  const [[x]] = [bad]
}
namespace N51 {
  declare const bad: { tag: 51; [Symbol.iterator](): { next: number } }
  const { nested: [x] } = { nested: bad }
}
namespace N52 {
  declare const bad: { tag: 52; [Symbol.iterator](): { next: number } }
  for (const x of bad) { const probe: never = x }
}
namespace N53 {
  declare const bad: { tag: 53; [Symbol.iterator](): { next: number } }
  const spread = [...bad]
}
namespace N54 {
  declare const bad: { tag: 54; [Symbol.iterator](): { next: number } }
  let current: string | number = 0
  while (Math.random() < 0.5) { const [x] = bad; const [y] = [current]; current = typeof y === 'string' ? 1 : (x as string) }
}
namespace N55 {
  declare const bad: { tag: 55; [Symbol.iterator](): { next: number } }
  let current: unknown = 0
  while (current) { const [x] = bad; current = x }
}
namespace N56 {
  declare const bad: { tag: 56; [Symbol.iterator](): { next: number } }
  let current: unknown = 0
  while (current) { const [x, y = x] = bad; for (const z of bad) { current = z ?? y } }
}
namespace N57 {
  declare const bad: { tag: 57; [Symbol.iterator](): { next: number } }
  const f = () => { const [x] = bad; return x }
  const [y] = bad
}
namespace N58 {
  declare const bad: { tag: 58; [Symbol.iterator](): { next: number } }
  function* g() { yield* bad }
}
namespace N59 {
  declare const bad: { tag: 59; [Symbol.iterator](): { next: number } }
  const [x] = bad
  const [y] = bad
}
namespace N60 {
  declare const bad: { tag: 60; [Symbol.iterator](): { next: number } }
  let x
  while (Math.random() < 0.5) { [x] = bad }
}
namespace N61 {
  declare const bad: { tag: 61; [Symbol.iterator](): { next(): { done?: boolean } } }
  function early() { return [x, y] }
  const [x, y] = bad
}
namespace N62 {
  declare const bad: { tag: 62; [Symbol.iterator](): { next(): { done?: boolean } } }
  const [x, y = x] = bad
}
namespace N63 {
  declare const bad: { tag: 63; [Symbol.iterator](): { next(): { done?: boolean } } }
  let [x] = bad
  if (typeof x === 'string') { const f = () => x }
}
namespace N64 {
  declare const bad: { tag: 64; [Symbol.iterator](): { next(): { done?: boolean } } }
  while (Math.random() < 0.5) { const [x, y] = bad; const probe: never = x }
}
namespace N65 {
  declare const bad: { tag: 65; [Symbol.iterator](): { next(): { done?: boolean } } }
  for (const [x] of [bad]) { const probe: never = x }
}
namespace N66 {
  declare const bad: { tag: 66; [Symbol.iterator](): { next(): { done?: boolean } } }
  function params([x, y]: typeof bad) { return x ?? y }
}
namespace N67 {
  declare const bad: { tag: 67; [Symbol.iterator](): { next(): { done?: boolean } } }
  function defaults([x] = bad) { return x }
}
namespace N68 {
  declare const bad: { tag: 68; [Symbol.iterator](): { next(): { done?: boolean } } }
  let x, y
  ;[x, y] = bad
}
namespace N69 {
  declare const bad: { tag: 69; [Symbol.iterator](): { next(): { done?: boolean } } }
  let x, y
  ;[x = 1, ...y] = bad
}
namespace N70 {
  declare const bad: { tag: 70; [Symbol.iterator](): { next(): { done?: boolean } } }
  const [[x]] = [bad]
}
namespace N71 {
  declare const bad: { tag: 71; [Symbol.iterator](): { next(): { done?: boolean } } }
  const { nested: [x] } = { nested: bad }
}
namespace N72 {
  declare const bad: { tag: 72; [Symbol.iterator](): { next(): { done?: boolean } } }
  for (const x of bad) { const probe: never = x }
}
namespace N73 {
  declare const bad: { tag: 73; [Symbol.iterator](): { next(): { done?: boolean } } }
  const spread = [...bad]
}
namespace N74 {
  declare const bad: { tag: 74; [Symbol.iterator](): { next(): { done?: boolean } } }
  let current: string | number = 0
  while (Math.random() < 0.5) { const [x] = bad; const [y] = [current]; current = typeof y === 'string' ? 1 : (x as string) }
}
namespace N75 {
  declare const bad: { tag: 75; [Symbol.iterator](): { next(): { done?: boolean } } }
  let current: unknown = 0
  while (current) { const [x] = bad; current = x }
}
namespace N76 {
  declare const bad: { tag: 76; [Symbol.iterator](): { next(): { done?: boolean } } }
  let current: unknown = 0
  while (current) { const [x, y = x] = bad; for (const z of bad) { current = z ?? y } }
}
namespace N77 {
  declare const bad: { tag: 77; [Symbol.iterator](): { next(): { done?: boolean } } }
  const f = () => { const [x] = bad; return x }
  const [y] = bad
}
namespace N78 {
  declare const bad: { tag: 78; [Symbol.iterator](): { next(): { done?: boolean } } }
  function* g() { yield* bad }
}
namespace N79 {
  declare const bad: { tag: 79; [Symbol.iterator](): { next(): { done?: boolean } } }
  const [x] = bad
  const [y] = bad
}
namespace N80 {
  declare const bad: { tag: 80; [Symbol.iterator](): { next(): { done?: boolean } } }
  let x
  while (Math.random() < 0.5) { [x] = bad }
}
export const sentinel: number = ''
// @filename: literal_properties_in_a_circle.ts
export {}
const a = { a }
const b = { ...b }
const c = { c: c }
const d = { get d() { return d } }
const e = { e() { return e } }
const f = { ['f']: f }
const g = { g, other: 1 }
declare function wrap<T>(value: T): T
const h = wrap({ h })
const i = { nested: { i } }
let j = { j }
while (j) { j = { j } }
const k = { k: () => k }
const l = [{ l }]
function m(n = { n }) {}
class P { q = { q: this.q } }
const r = { r: { get s() { return r } } }
export const sentinel: number = ''
// @filename: rest_in_an_array_pattern.ts
export {}
declare const value: [string | number, string | number, string | number]
if (typeof value[1] === 'string') {
  const [head, ...[second]] = value
  const probe: never = second
}
if (typeof value[1] === 'string') {
  const [head, ...[second, third]] = value
  const probe: never = second
  const probe3: never = third
}
if (typeof value[2] === 'string') {
  const [head, ...[second, third]] = value
  const probe3: never = third
}
if (typeof value[2] === 'string') {
  const [, , ...[third]] = value
  const probe3: never = third
}
if (typeof value[2] === 'string') {
  const [a, ...[b, ...[c]]] = value
  const probe: never = c
}
if (typeof value[1] === 'string') {
  const [head, ...rest] = value
  const probe: never = rest
  const [second] = rest
  const probe2: never = second
}
declare const pair: [[string | number, string | number], string | number]
if (typeof pair[0][1] === 'string') {
  const [[x, ...[y]]] = pair
  const probe: never = y
}
if (typeof value[1] === 'string') {
  let second
  ;[, ...[second]] = value
  const probe: never = second
}
declare const obj: { list: [string | number, string | number] }
if (typeof obj.list[1] === 'string') {
  const { list: [first, ...[second]] } = obj
  const probe: never = second
}
export const sentinel: number = ''
