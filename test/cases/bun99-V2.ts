// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "esnext", "module": "esnext", "lib": ["esnext"], "types": [], "moduleDetection": "force"}, "files": ["derived.ts", "rest.ts", "enums.ts", "rest2.ts", "variable.ts", "enums2.ts", "widened.ts", "nested.ts", "namespaces.ts"]}
// @filename: derived.ts
declare namespace N { const id: string | number; let other: 1 }
declare const b1: new () => Partial<typeof N> & typeof N
class C1 extends b1 { id = 'x' }
class C2 extends b1 {}
declare const b3: new () => { other?: 1 } & typeof N
class C3 extends b3 { id = 'x' }
declare const b4: new () => { other?: 1 } & { other: 1; id: string }
class C4 extends b4 { id = 'x' }
export const p1: never = new C1().other
export const p3: never = new C3().other
// @filename: enums.ts
enum E { id, other }
const { other, ...rest } = E
export const p1: never = null! as keyof typeof rest
export const p2: never = rest
const spread = { ...E }
export const p3: never = null! as keyof typeof spread
export const p4: never = spread
export const p5: never = null! as keyof { [K in keyof typeof E]: 1 }
declare const m: { [K in keyof typeof E]: (typeof E)[K] }
export const p6: never = m
export const p7: never = null! as keyof typeof m
export const p8: never = null! as keyof (typeof E & { x: 1 })
export const p9: never = null! as keyof Readonly<typeof E>
const two = { ...E, ...{ x: 1 } }
export const p10: never = null! as keyof typeof two
export const p11: never = rest[0]
export const p12: never = spread[0]
// @filename: enums2.ts
enum E { id, other, third }
enum S { id = 'a', other = 'b' }
enum Mixed { id, other = 'b' }
const enum CE { id, other }
const { other, ...r1 } = E
const { id, ...r2 } = r1
export const p1: never = null! as keyof typeof r2
export const p2: never = r2
const s1 = { ...r1 }
export const p3: never = null! as keyof typeof s1
export const p4: never = null! as keyof (typeof r1 & { x: 1 })
export const p5: never = null! as keyof (typeof r1 | { id: 1 })
export const p6: never = null! as { [K in keyof typeof r1]: 1 }
export const p7: never = null! as Readonly<typeof r1>
export const p8: never = null! as keyof Readonly<typeof r1>
const { other: o2, ...rs } = S
export const p9: never = null! as keyof typeof rs
const { other: o3, ...rm } = Mixed
export const p10: never = null! as keyof typeof rm
export const p11: never = rm
declare function values<T>(x: { [K in keyof T]: T[K] }): T
export const p12: never = values(r1)
declare function record<V>(x: { [k: string]: V }): V
export const p13: never = record(r1)
export const p14: never = record(E)
declare function mapped<K extends PropertyKey, V>(x: { [P in K]: V }): [K, V]
export const p15: never = mapped(r1)
export const p16: never = mapped(E)
export const p17: never = r1[0]
function f({ other, ...r }: typeof E) { const p: never = null! as keyof typeof r }
let a; ({ other: a, ...a } = E)
declare const eu: typeof E | typeof S
const { other: o4, ...ru } = eu
export const p18: never = null! as keyof typeof ru
export const p19: never = ru
const target: { readonly [x: number]: string } = r1
export const p20: never = Object.keys(r1)
for (const k in r1) { const p: never = k }
// @filename: namespaces.ts
declare namespace A { let x: 1; const y: 2; function f(): void; class K {} }
declare namespace B { let x: 1; const y: 2; function f(): void; class K {} }
declare const v: typeof A & typeof B
const r: { x: 1; y: 2 } = v
export const p1: never = v.x
export const p2: never = null! as { [K in keyof typeof v]: (typeof v)[K] }
export const p3: never = null! as Required<typeof v>
v.x = 1
v.y = 2
enum E1 { m }
enum E2 { m }
declare const e: typeof E1 & typeof E2
const re: { m: unknown } = e
export const p4: never = null! as { [K in keyof typeof e]: 1 }
declare const three: typeof A & typeof B & { x?: 1 }
const r3: { x: 1 } = three
export const p5: never = null! as { [K in keyof typeof three]: (typeof three)[K] }
declare const mixed: typeof A & { x: 1 }
const r4: { x: 1 } = mixed
declare const opt: typeof A & typeof B & { x?: 1 } & { x?: 1 }
export const p6: never = opt.x
declare const base: new () => typeof A & typeof B
class D extends base { own = 1 }
const spread = { ...v }
export const p7: never = spread
declare const u: (typeof A & typeof B) | { x: 2 }
export const p8: never = u.x
// @filename: nested.ts
declare class A { protected x: number }
interface B { x: number }
type M = { [K in 'x']: number }
declare const v: A & (B | M)
v.x
declare const v2: (B | M) & A
v2.x
declare const v3: A & (M | M)
v3.x
declare const v4: (A & M) | (A & { [K in 'x']: string })
v4.x
declare const v5: (A & B) | (A & B & M)
v5.x
declare class P { private x: number }
declare const v6: P & (B | M)
v6.x
declare class R { readonly x: number }
declare const v7: R & (B | M)
v7.x = 1
declare const v8: (R & M) & (R & M & { y: 1 })
v8.x = 1
export const k1: never = null! as keyof typeof v
export const k7: never = null! as { [K in keyof typeof v7]: (typeof v7)[K] }
class Sub extends A { m(a: Sub & (B | M), b: A & (B | M)) { a.x; b.x } }
// @filename: rest.ts
declare const u: { a: 1; id: string } | { a: 2; id: string }
const { a, ...rest } = u
export const p1: never = rest
declare const v: { a: 1; id: string }
const { a: a1, ...r1 } = v
const { a: a2, ...r2 } = v
export const p2: never = null! as typeof r1 | typeof r2
export const p3: never = [r1, r2]
declare const c: boolean
export const p4: never = c ? r1 : r2
function f<T extends { a: 1 }>(t: T | { a: 1; id: string } | { a: 1; id: string; b?: 1 }) { const { a, ...r } = t; const p: never = r }
const s1 = { ...v }, s2 = { ...v }
export const p5: never = null! as typeof s1 | typeof s2
declare const w: { id: string } | { id: string; a?: 1 }
const s3 = { ...w }
export const p6: never = s3
// @filename: rest2.ts
interface D { readonly id: string; a: 1 }
type Same<T> = { [K in keyof T]: T[K] }
type Writable<T> = { -readonly [K in keyof T]: T[K] }
declare const u: D | Same<D> | Writable<D>
const { a, ...r1 } = u
export const p1: never = r1
export const p2: never = null! as keyof typeof r1
function param({ a, ...r }: D | Same<D>) { const p: never = r }
let r3; ({ a: r3, ...r3 } = u)
let a4: 1, r4: { id: string }
;({ a: a4, ...r4 } = u)
export const p5: never = [r1]
declare const c: boolean
export const p6: never = c ? r1 : { id: '' }
const { id, ...r7 } = u
export const p7: never = r7
function generic<T extends D>(t: T | D | Same<D>) { const { a, ...r } = t; const p: never = r }
for (const { a, ...r } of [u]) { const p: never = r }
const { a: a8, ...r8 } = r1 as typeof r1 & { a: 1 }
export const p8: never = r8
const copy = { ...r1 }
export const p9: never = copy
export const p10: never = null! as Same<typeof r1>
declare function first<T>(x: T, y: T): T
export const p11: never = first(r1, r1)
const assignable: { id: string } = r1
declare const nested: { x: D | Same<D> }
const { x: { a: a12, ...r12 } } = nested
export const p12: never = r12
// @filename: variable.ts
declare namespace N { let other: 1; const id: 2; function f(): void; class K {} }
declare const b1: new () => { other?: 1; id?: 2; f?: () => void } & typeof N
class C1 extends b1 { own = 1 }
export const p1: never = new C1().other
export const p2: never = new C1().id
export const p3: never = new C1().f
declare const x: { other?: 1 } & typeof N
interface I1 extends Pick<typeof x, 'other'> { own: 1 }
declare const i1: I1
export const p4: never = i1.other
type M = { [K in keyof typeof x]: (typeof x)[K] }
export const p5: never = null! as M
export const p6: never = null! as Required<typeof x>
export const p7: never = null! as Partial<typeof x>
const req: { other: 1 } = new C1()
const back: C1 = null! as { other: 1; id: 2; f(): void; own: number; K: typeof N.K }
const spread = { ...x }
export const p8: never = spread
const { id, ...rest } = x
export const p9: never = rest
export const p10: never = x['other']
function g<T extends typeof x>(t: T) { const p: never = t.other }
declare const g1: new () => { Array?: 1 } & typeof globalThis
class C2 extends g1 { own = 1 }
declare const y: { other?: 1 } & { other?: 1 } & typeof N
export const p11: never = y.other
declare const b3: new () => typeof y
class C3 extends b3 { own = 1 }
declare const z: ({ other?: 1 } & typeof N) | { other: 2 }
export const p12: never = z.other
// @filename: widened.ts
enum E { A, B }
const x = { ...E } as const
type K = keyof typeof x
const k: K = 0
export const p1: never = null! as K
export const p2: never = x
let y = { ...E } as const
export const p3: never = null! as keyof typeof y
const z = { ...E }
export const p4: never = null! as keyof typeof z
const { A, ...rest } = E
let w = rest
export const p5: never = null! as keyof typeof w
const s = { ...rest } as const
export const p6: never = null! as keyof typeof s
const t = [{ ...E } as const]
export const p7: never = null! as keyof (typeof t)[0]
function f() { return { ...E } as const }
export const p8: never = null! as keyof ReturnType<typeof f>
const n = { inner: { ...E } } as const
export const p9: never = null! as keyof typeof n.inner
const r = { ...E, ...{ x: 1 } } as const
export const p10: never = null! as keyof typeof r
declare function id<T>(t: T): T
export const p11: never = null! as keyof ReturnType<typeof id<typeof x>>
const viaId = id({ ...E } as const)
export const p12: never = null! as keyof typeof viaId
