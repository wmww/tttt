// @strict: true
// `[K in keyof U]` over a union U that isn't generic goes by the union's properties (tsc's
// forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType over getPropertiesOfType), not `keyof U`: a property some
// member has public is a key even when another member declares it protected (keyof drops it), a protected one the
// members don't share isn't a property at all, and the index signatures are the union's (string, no number)
type Writable<T> = { -readonly [K in keyof T]: T[K] }
declare class D { protected readonly id: string | number; other: 1 }
type W = Writable<Pick<D, 'id'>>
declare const u: W | D
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'

declare class D2 { protected id: string | number; other: 1 }
type W2 = { +readonly [K in keyof D2]: D2[K] }
declare const dju: D2 & ({ id: string | number } | W2)
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'

type Plain = { id: string | number }
declare const p: Plain | D
declare const p_mapped: { [K in keyof typeof p]: (typeof p)[K] }
p_mapped.id = 'x'

declare const s: { [k: string]: number } | { [k: string]: string; a: 1 }
declare const s_mapped: { [K in keyof typeof s]: (typeof s)[K] }
const s_printed: never = s_mapped
