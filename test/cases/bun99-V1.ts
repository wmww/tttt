// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "esnext", "module": "esnext", "lib": ["esnext"], "types": [], "moduleDetection": "force"}, "files": ["as_const__alias_minus_readonly.ts", "as_const__as_clause.ts", "class__alias_minus_readonly.ts", "class__as_clause_renamed.ts", "class__homomorphic.ts", "enum__literal_keys.ts", "enum__minus_readonly.ts", "generic__omit.ts", "generic__other_interface.ts", "getter__other_readonly_interface.ts", "getter__partial.ts", "getter_setter__pick_of_writable.ts", "getter_setter__plain.ts", "inherited__plain_readonly.ts", "inherited__plus_readonly.ts", "interface__alias_minus_readonly.ts", "interface__record.ts", "interface__required.ts", "interface_getter__spread.ts", "interface_getter__twice_mapped.ts", "merged_interface__alias_minus_readonly.ts", "merged_interface__writable_of_pick.ts", "namespace_const__as_clause.ts", "namespace_const__as_clause_renamed.ts", "namespace_const__spread.ts", "nested_intersection__homomorphic.ts", "nested_intersection__literal_keys.ts", "optional__minus_readonly.ts", "optional__omit.ts", "parameter_property__other_interface.ts", "parameter_property__other_readonly_interface.ts", "private_class__literal_keys.ts", "private_class__partial.ts", "private_class__pick_of_writable.ts", "private_class__record.ts", "protected_class__plain.ts", "protected_class__plain_readonly.ts", "protected_class__record.ts", "protected_class__writable_of_pick.ts", "protected_writable_class__plus_readonly.ts", "protected_writable_class__record.ts", "readonly_mapped__required.ts", "readonly_mapped__spread.ts", "static_class__twice_mapped.ts", "static_class__writable_of_pick.ts", "type_literal__alias_minus_readonly.ts", "type_literal__as_clause.ts", "writable__as_clause_renamed.ts", "writable__homomorphic.ts"]}
// @filename: as_const__alias_minus_readonly.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

const d0 = { id: 'x' as string | number, other: 1 } as const
type D = typeof d0
declare const d: D
const spread = { ...d }
type W = Writable<D>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: as_const__as_clause.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

const d0 = { id: 'x' as string | number, other: 1 } as const
type D = typeof d0
declare const d: D
const spread = { ...d }
type W = { -readonly [K in keyof D as K]: D[K] }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: class__alias_minus_readonly.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Writable<D>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: class__as_clause_renamed.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = { -readonly [K in 'other' as 'id']: string | number }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: class__homomorphic.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = { [K in keyof D]: D[K] }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: enum__literal_keys.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

enum E { id, other }
type D = typeof E
declare const d: D
const spread = { ...d }
type W = { -readonly [K in 'id']: string | number }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: enum__minus_readonly.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

enum E { id, other }
type D = typeof E
declare const d: D
const spread = { ...d }
type W = { -readonly [K in keyof D]: D[K] }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: generic__omit.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface G<T> { readonly id: T; other: 1 }
type D = G<string | number>
declare const d: D
const spread = { ...d }
type W = Omit<Writable<D>, 'other'>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: generic__other_interface.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface G<T> { readonly id: T; other: 1 }
type D = G<string | number>
declare const d: D
const spread = { ...d }
type W = J
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: getter__other_readonly_interface.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { get id(): string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = JR
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: getter__partial.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { get id(): string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Partial<Writable<D>>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: getter_setter__pick_of_writable.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { get id(): string | number; set id(v: string | number); other: 1 }
declare const d: D
const spread = { ...d }
type W = Pick<Writable<D>, 'id'>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: getter_setter__plain.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { get id(): string | number; set id(v: string | number); other: 1 }
declare const d: D
const spread = { ...d }
type W = { id: string | number }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: inherited__plain_readonly.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface B0 { readonly id: string | number }
interface D extends B0 { other: 1 }
declare const d: D
const spread = { ...d }
type W = { readonly id: string | number }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: inherited__plus_readonly.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface B0 { readonly id: string | number }
interface D extends B0 { other: 1 }
declare const d: D
const spread = { ...d }
type W = { +readonly [K in keyof D]: D[K] }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: interface__alias_minus_readonly.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D { readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Writable<D>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: interface__record.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D { readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Record<'id', string | number>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: interface__required.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D { readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Required<Writable<D>>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: interface_getter__spread.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D { get id(): string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = typeof spread
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: interface_getter__twice_mapped.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D { get id(): string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Writable<Writable<D>>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: merged_interface__alias_minus_readonly.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D { readonly id: string | number }
interface D { other: 1 }
declare const d: D
const spread = { ...d }
type W = Writable<D>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: merged_interface__writable_of_pick.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D { readonly id: string | number }
interface D { other: 1 }
declare const d: D
const spread = { ...d }
type W = Writable<Pick<D, 'id'>>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: namespace_const__as_clause.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare namespace N { const id: string | number; let other: 1 }
type D = typeof N
declare const d: D
const spread = { ...d }
type W = { -readonly [K in keyof D as K]: D[K] }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: namespace_const__as_clause_renamed.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare namespace N { const id: string | number; let other: 1 }
type D = typeof N
declare const d: D
const spread = { ...d }
type W = { -readonly [K in 'other' as 'id']: string | number }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: namespace_const__spread.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare namespace N { const id: string | number; let other: 1 }
type D = typeof N
declare const d: D
const spread = { ...d }
type W = typeof spread
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: nested_intersection__homomorphic.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D1 { readonly id: string | number; other: 1 }
type D = D1 & { -readonly [K in keyof D1]: D1[K] }
declare const d: D
const spread = { ...d }
type W = { [K in keyof D]: D[K] }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: nested_intersection__literal_keys.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D1 { readonly id: string | number; other: 1 }
type D = D1 & { -readonly [K in keyof D1]: D1[K] }
declare const d: D
const spread = { ...d }
type W = { -readonly [K in 'id']: string | number }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: optional__minus_readonly.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D { readonly id?: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = { -readonly [K in keyof D]: D[K] }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: optional__omit.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D { readonly id?: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Omit<Writable<D>, 'other'>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: parameter_property__other_interface.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

class D { other: 1 = 1; constructor(readonly id: string | number) {} }
declare const d: D
const spread = { ...d }
type W = J
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: parameter_property__other_readonly_interface.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

class D { other: 1 = 1; constructor(readonly id: string | number) {} }
declare const d: D
const spread = { ...d }
type W = JR
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: private_class__literal_keys.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { private readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = { -readonly [K in 'id']: string | number }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: private_class__partial.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { private readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Partial<Writable<D>>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: private_class__pick_of_writable.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { private readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Pick<Writable<D>, 'id'>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: private_class__record.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { private readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Record<'id', string | number>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: protected_class__plain.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { protected readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = { id: string | number }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: protected_class__plain_readonly.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { protected readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = { readonly id: string | number }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: protected_class__record.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { protected readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Record<'id', string | number>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: protected_class__writable_of_pick.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { protected readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Writable<Pick<D, 'id'>>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: protected_writable_class__plus_readonly.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { protected id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = { +readonly [K in keyof D]: D[K] }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: protected_writable_class__record.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class D { protected id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Record<'id', string | number>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: readonly_mapped__required.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D0 { id: string | number; other: 1 }
type D = Readonly<D0>
declare const d: D
const spread = { ...d }
type W = Required<Writable<D>>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: readonly_mapped__spread.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D0 { id: string | number; other: 1 }
type D = Readonly<D0>
declare const d: D
const spread = { ...d }
type W = typeof spread
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: static_class__twice_mapped.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class C0 { static readonly id: string | number; static other: 1 }
type D = typeof C0
declare const d: D
const spread = { ...d }
type W = Writable<Writable<D>>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: static_class__writable_of_pick.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

declare class C0 { static readonly id: string | number; static other: 1 }
type D = typeof C0
declare const d: D
const spread = { ...d }
type W = Writable<Pick<D, 'id'>>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: type_literal__alias_minus_readonly.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

type D = { readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = Writable<D>
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: type_literal__as_clause.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

type D = { readonly id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = { -readonly [K in keyof D as K]: D[K] }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: writable__as_clause_renamed.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D { id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = { -readonly [K in 'other' as 'id']: string | number }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
// @filename: writable__homomorphic.ts
type Writable<T> = { -readonly [K in keyof T]: T[K] }
interface J { id: string | number }
interface JR { readonly id: string | number }
declare const pub: { id: string | number; other: 1 }

interface D { id: string | number; other: 1 }
declare const d: D
const spread = { ...d }
type W = { [K in keyof D]: D[K] }
declare const wd: W & D
wd.id = 'x'
wd['id'] = 'x'
wd.id += 'x'
delete wd.id
;({ id: wd.id } = { id: 'x' })
;[wd.id] = ['x']
for (wd.id of ['x']) {}
for (wd.id in {}) {}
declare const wd_mapped: { [K in keyof typeof wd]: (typeof wd)[K] }
wd_mapped.id = 'x'
const wd_printed: never = wd_mapped
declare const wd_writable: Writable<typeof wd>
wd_writable.id = 'x'
const wd_spread = { ...wd }
wd_spread.id = 'x'
const wd_is_string = typeof wd.id === 'string'
if (wd_is_string) { const narrowed: never = wd.id }
function wd_generic<T extends typeof wd>(t: T) { t.id = 'x' }
const { other: wd_other, ...wd_rest } = wd
const wd_rest_printed: never = wd_rest
const wd_to_public: { id: string | number } = wd
const wd_from_public: typeof wd = pub
function wd_indexed<T extends typeof wd>(x: T['id']) {}
declare const wd_base: new () => typeof wd
class wd_derived extends wd_base { id = 'x' }
declare const wd_or_empty: typeof wd | {}
const wd_merged = { ...wd_or_empty }
const wd_merged_printed: never = wd_merged
declare const wd_keys: keyof typeof wd
const wd_keys_printed: never = wd_keys

declare const dw: D & W
dw.id = 'x'
dw['id'] = 'x'
dw.id += 'x'
delete dw.id
;({ id: dw.id } = { id: 'x' })
;[dw.id] = ['x']
for (dw.id of ['x']) {}
for (dw.id in {}) {}
declare const dw_mapped: { [K in keyof typeof dw]: (typeof dw)[K] }
dw_mapped.id = 'x'
const dw_printed: never = dw_mapped
declare const dw_writable: Writable<typeof dw>
dw_writable.id = 'x'
const dw_spread = { ...dw }
dw_spread.id = 'x'
const dw_is_string = typeof dw.id === 'string'
if (dw_is_string) { const narrowed: never = dw.id }
function dw_generic<T extends typeof dw>(t: T) { t.id = 'x' }
const { other: dw_other, ...dw_rest } = dw
const dw_rest_printed: never = dw_rest
const dw_to_public: { id: string | number } = dw
const dw_from_public: typeof dw = pub
function dw_indexed<T extends typeof dw>(x: T['id']) {}
declare const dw_base: new () => typeof dw
class dw_derived extends dw_base { id = 'x' }
declare const dw_or_empty: typeof dw | {}
const dw_merged = { ...dw_or_empty }
const dw_merged_printed: never = dw_merged
declare const dw_keys: keyof typeof dw
const dw_keys_printed: never = dw_keys

declare const wdp: W & D & { id: string | number }
wdp.id = 'x'
wdp['id'] = 'x'
wdp.id += 'x'
delete wdp.id
;({ id: wdp.id } = { id: 'x' })
;[wdp.id] = ['x']
for (wdp.id of ['x']) {}
for (wdp.id in {}) {}
declare const wdp_mapped: { [K in keyof typeof wdp]: (typeof wdp)[K] }
wdp_mapped.id = 'x'
const wdp_printed: never = wdp_mapped
declare const wdp_writable: Writable<typeof wdp>
wdp_writable.id = 'x'
const wdp_spread = { ...wdp }
wdp_spread.id = 'x'
const wdp_is_string = typeof wdp.id === 'string'
if (wdp_is_string) { const narrowed: never = wdp.id }
function wdp_generic<T extends typeof wdp>(t: T) { t.id = 'x' }
const { other: wdp_other, ...wdp_rest } = wdp
const wdp_rest_printed: never = wdp_rest
const wdp_to_public: { id: string | number } = wdp
const wdp_from_public: typeof wdp = pub
function wdp_indexed<T extends typeof wdp>(x: T['id']) {}
declare const wdp_base: new () => typeof wdp
class wdp_derived extends wdp_base { id = 'x' }
declare const wdp_or_empty: typeof wdp | {}
const wdp_merged = { ...wdp_or_empty }
const wdp_merged_printed: never = wdp_merged
declare const wdp_keys: keyof typeof wdp
const wdp_keys_printed: never = wdp_keys

declare const wdw: W & D & Writable<D>
wdw.id = 'x'
wdw['id'] = 'x'
wdw.id += 'x'
delete wdw.id
;({ id: wdw.id } = { id: 'x' })
;[wdw.id] = ['x']
for (wdw.id of ['x']) {}
for (wdw.id in {}) {}
declare const wdw_mapped: { [K in keyof typeof wdw]: (typeof wdw)[K] }
wdw_mapped.id = 'x'
const wdw_printed: never = wdw_mapped
declare const wdw_writable: Writable<typeof wdw>
wdw_writable.id = 'x'
const wdw_spread = { ...wdw }
wdw_spread.id = 'x'
const wdw_is_string = typeof wdw.id === 'string'
if (wdw_is_string) { const narrowed: never = wdw.id }
function wdw_generic<T extends typeof wdw>(t: T) { t.id = 'x' }
const { other: wdw_other, ...wdw_rest } = wdw
const wdw_rest_printed: never = wdw_rest
const wdw_to_public: { id: string | number } = wdw
const wdw_from_public: typeof wdw = pub
function wdw_indexed<T extends typeof wdw>(x: T['id']) {}
declare const wdw_base: new () => typeof wdw
class wdw_derived extends wdw_base { id = 'x' }
declare const wdw_or_empty: typeof wdw | {}
const wdw_merged = { ...wdw_or_empty }
const wdw_merged_printed: never = wdw_merged
declare const wdw_keys: keyof typeof wdw
const wdw_keys_printed: never = wdw_keys

declare const u: W | D
u.id = 'x'
u['id'] = 'x'
u.id += 'x'
delete u.id
;({ id: u.id } = { id: 'x' })
;[u.id] = ['x']
for (u.id of ['x']) {}
for (u.id in {}) {}
declare const u_mapped: { [K in keyof typeof u]: (typeof u)[K] }
u_mapped.id = 'x'
const u_printed: never = u_mapped
declare const u_writable: Writable<typeof u>
u_writable.id = 'x'
const u_spread = { ...u }
u_spread.id = 'x'
const u_is_string = typeof u.id === 'string'
if (u_is_string) { const narrowed: never = u.id }
function u_generic<T extends typeof u>(t: T) { t.id = 'x' }
const { other: u_other, ...u_rest } = u
const u_rest_printed: never = u_rest
const u_to_public: { id: string | number } = u
const u_from_public: typeof u = pub
function u_indexed<T extends typeof u>(x: T['id']) {}
declare const u_base: new () => typeof u
class u_derived extends u_base { id = 'x' }
declare const u_or_empty: typeof u | {}
const u_merged = { ...u_or_empty }
const u_merged_printed: never = u_merged
declare const u_keys: keyof typeof u
const u_keys_printed: never = u_keys

declare const w: W
w.id = 'x'
w['id'] = 'x'
w.id += 'x'
delete w.id
;({ id: w.id } = { id: 'x' })
;[w.id] = ['x']
for (w.id of ['x']) {}
for (w.id in {}) {}
declare const w_mapped: { [K in keyof typeof w]: (typeof w)[K] }
w_mapped.id = 'x'
const w_printed: never = w_mapped
declare const w_writable: Writable<typeof w>
w_writable.id = 'x'
const w_spread = { ...w }
w_spread.id = 'x'
const w_is_string = typeof w.id === 'string'
if (w_is_string) { const narrowed: never = w.id }
function w_generic<T extends typeof w>(t: T) { t.id = 'x' }
const { other: w_other, ...w_rest } = w
const w_rest_printed: never = w_rest
const w_to_public: { id: string | number } = w
const w_from_public: typeof w = pub
function w_indexed<T extends typeof w>(x: T['id']) {}
declare const w_base: new () => typeof w
class w_derived extends w_base { id = 'x' }
declare const w_or_empty: typeof w | {}
const w_merged = { ...w_or_empty }
const w_merged_printed: never = w_merged
declare const w_keys: keyof typeof w
const w_keys_printed: never = w_keys

declare const dju: D & (J | W)
dju.id = 'x'
dju['id'] = 'x'
dju.id += 'x'
delete dju.id
;({ id: dju.id } = { id: 'x' })
;[dju.id] = ['x']
for (dju.id of ['x']) {}
for (dju.id in {}) {}
declare const dju_mapped: { [K in keyof typeof dju]: (typeof dju)[K] }
dju_mapped.id = 'x'
const dju_printed: never = dju_mapped
declare const dju_writable: Writable<typeof dju>
dju_writable.id = 'x'
const dju_spread = { ...dju }
dju_spread.id = 'x'
const dju_is_string = typeof dju.id === 'string'
if (dju_is_string) { const narrowed: never = dju.id }
function dju_generic<T extends typeof dju>(t: T) { t.id = 'x' }
const { other: dju_other, ...dju_rest } = dju
const dju_rest_printed: never = dju_rest
const dju_to_public: { id: string | number } = dju
const dju_from_public: typeof dju = pub
function dju_indexed<T extends typeof dju>(x: T['id']) {}
declare const dju_base: new () => typeof dju
class dju_derived extends dju_base { id = 'x' }
declare const dju_or_empty: typeof dju | {}
const dju_merged = { ...dju_or_empty }
const dju_merged_printed: never = dju_merged
declare const dju_keys: keyof typeof dju
const dju_keys_printed: never = dju_keys

declare const udw: (D & W) | (D & W & { more: 1 })
udw.id = 'x'
udw['id'] = 'x'
udw.id += 'x'
delete udw.id
;({ id: udw.id } = { id: 'x' })
;[udw.id] = ['x']
for (udw.id of ['x']) {}
for (udw.id in {}) {}
declare const udw_mapped: { [K in keyof typeof udw]: (typeof udw)[K] }
udw_mapped.id = 'x'
const udw_printed: never = udw_mapped
declare const udw_writable: Writable<typeof udw>
udw_writable.id = 'x'
const udw_spread = { ...udw }
udw_spread.id = 'x'
const udw_is_string = typeof udw.id === 'string'
if (udw_is_string) { const narrowed: never = udw.id }
function udw_generic<T extends typeof udw>(t: T) { t.id = 'x' }
const { other: udw_other, ...udw_rest } = udw
const udw_rest_printed: never = udw_rest
const udw_to_public: { id: string | number } = udw
const udw_from_public: typeof udw = pub
function udw_indexed<T extends typeof udw>(x: T['id']) {}
declare const udw_base: new () => typeof udw
class udw_derived extends udw_base { id = 'x' }
declare const udw_or_empty: typeof udw | {}
const udw_merged = { ...udw_or_empty }
const udw_merged_printed: never = udw_merged
declare const udw_keys: keyof typeof udw
const udw_keys_printed: never = udw_keys

declare const udj: (D & J) | (D & W)
udj.id = 'x'
udj['id'] = 'x'
udj.id += 'x'
delete udj.id
;({ id: udj.id } = { id: 'x' })
;[udj.id] = ['x']
for (udj.id of ['x']) {}
for (udj.id in {}) {}
declare const udj_mapped: { [K in keyof typeof udj]: (typeof udj)[K] }
udj_mapped.id = 'x'
const udj_printed: never = udj_mapped
declare const udj_writable: Writable<typeof udj>
udj_writable.id = 'x'
const udj_spread = { ...udj }
udj_spread.id = 'x'
const udj_is_string = typeof udj.id === 'string'
if (udj_is_string) { const narrowed: never = udj.id }
function udj_generic<T extends typeof udj>(t: T) { t.id = 'x' }
const { other: udj_other, ...udj_rest } = udj
const udj_rest_printed: never = udj_rest
const udj_to_public: { id: string | number } = udj
const udj_from_public: typeof udj = pub
function udj_indexed<T extends typeof udj>(x: T['id']) {}
declare const udj_base: new () => typeof udj
class udj_derived extends udj_base { id = 'x' }
declare const udj_or_empty: typeof udj | {}
const udj_merged = { ...udj_or_empty }
const udj_merged_printed: never = udj_merged
declare const udj_keys: keyof typeof udj
const udj_keys_printed: never = udj_keys

function generic<T extends D>(a: Writable<T> & T, b: T & Writable<T>) {
  a.id = 'x'
  b.id = 'x'
}
export const sentinel: number = ""
