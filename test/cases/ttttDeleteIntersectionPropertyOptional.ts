// @strict: true
// @exactOptionalPropertyTypes: true,false
// @noTypesAndSymbols: true
// TS2790 for a property of an intersection: optional only when all its members' are, its type the intersection of
// theirs (tsc's createUnionOrIntersectionProperty, checkDeleteExpressionMustBeOptional)
type Writable<T> = { -readonly [K in keyof T]: T[K] }
declare class D { get id(): string | number; other: 1 }
type W = Partial<Writable<D>>
declare const wd: W & D
delete wd.id
declare const dw: D & W
delete dw.id
interface E { readonly id?: string | number; other: 1 }
type V = Omit<Writable<E>, 'other'>
declare const wdp: V & E & { id: string | number }
delete wdp.id
declare const x: { a?: 1 } & { a: 1 }
delete x.a
declare const y: { a?: 1 } & { readonly a?: 1 }
delete y.a
declare const z: { a: unknown } & { a: 1 }
delete z.a
declare const w: { a: never } | { a: 1 }
delete w.a
declare const v: { a: any } & { a: 1 }
delete v.a
declare const u: { a: unknown } | { a: 1 }
delete u.a
