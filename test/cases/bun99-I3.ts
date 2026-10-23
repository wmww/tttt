// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "es2022", "lib": ["es2022"], "types": []}, "files": ["repro.ts", "method__fnbranch_minusro_setreq_kind.ts", "method__roarray_fnbranch_minusro_setreq_kind.ts", "method__roarray_fnbranch_setreq_kind.ts", "method__this_fnbranch_minusro_setreq_kind.ts", "method__this_fnbranch_setreq_kind.ts", "method__this_minusro_setreq_kind.ts", "method__this_roarray_fnbranch_minusro_setreq_kind.ts", "method__this_roarray_fnbranch_minusro_setreq_kind_overload.ts", "method__this_roarray_fnbranch_setreq_kind.ts", "method__this_roarray_minusro_setreq_kind.ts", "method__this_roarray_setreq_kind.ts", "property__fnbranch_minusro_kind.ts", "property__fnbranch_setreq_kind.ts", "property__roarray_fnbranch_minusro_kind.ts", "property__roarray_fnbranch_minusro_setreq_kind.ts", "property__roarray_fnbranch_setreq_kind.ts", "property__this_fnbranch_kind.ts", "property__this_fnbranch_minusro_kind.ts", "property__this_fnbranch_minusro_setreq_kind.ts", "property__this_fnbranch_setreq_kind.ts", "property__this_minusro_kind.ts", "property__this_roarray_fnbranch_kind.ts", "property__this_roarray_fnbranch_minusro_kind.ts", "property__this_roarray_fnbranch_minusro_kind_overload.ts", "property__this_roarray_fnbranch_minusro_setreq_kind.ts", "property__this_roarray_fnbranch_minusro_setreq_kind_overload.ts", "property__this_roarray_fnbranch_setreq_kind.ts", "property__this_roarray_minusro_kind.ts", "property__this_roarray_minusro_setreq_kind.ts", "property__this_roarray_setreq_kind.ts", "property__this_setreq_kind.ts", "sentinel.ts"]}
// @filename: method__fnbranch_minusro_setreq_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: Item[] }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update(state: Draft<Item<K>>): void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: method__roarray_fnbranch_minusro_setreq_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update(state: Draft<Item<K>>): void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: method__roarray_fnbranch_setreq_kind.ts
type WritableDraft<T> = { [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update(state: Draft<Item<K>>): void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: method__this_fnbranch_minusro_setreq_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: Item[] }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update(state: Draft<this>): void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: method__this_fnbranch_setreq_kind.ts
type WritableDraft<T> = { [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: Item[] }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update(state: Draft<this>): void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: method__this_minusro_setreq_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: Item[] }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update(state: Draft<this>): void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: method__this_roarray_fnbranch_minusro_setreq_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update(state: Draft<this>): void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: method__this_roarray_fnbranch_minusro_setreq_kind_overload.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update(state: Draft<this>): void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>
declare function isItemA(item: WritableDraft<Item>): item is WritableDraft<ItemA>
declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: method__this_roarray_fnbranch_setreq_kind.ts
type WritableDraft<T> = { [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update(state: Draft<this>): void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: method__this_roarray_minusro_setreq_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update(state: Draft<this>): void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: method__this_roarray_setreq_kind.ts
type WritableDraft<T> = { [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update(state: Draft<this>): void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__fnbranch_minusro_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<T>
interface Group { readonly items?: Item[] }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<Item<K>>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__fnbranch_setreq_kind.ts
type WritableDraft<T> = { [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: Item[] }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<Item<K>>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__roarray_fnbranch_minusro_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<T>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<Item<K>>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__roarray_fnbranch_minusro_setreq_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<Item<K>>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__roarray_fnbranch_setreq_kind.ts
type WritableDraft<T> = { [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<Item<K>>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_fnbranch_kind.ts
type WritableDraft<T> = { [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<T>
interface Group { readonly items?: Item[] }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_fnbranch_minusro_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<T>
interface Group { readonly items?: Item[] }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_fnbranch_minusro_setreq_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: Item[] }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_fnbranch_setreq_kind.ts
type WritableDraft<T> = { [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: Item[] }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_minusro_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<T>
interface Group { readonly items?: Item[] }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_roarray_fnbranch_kind.ts
type WritableDraft<T> = { [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<T>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_roarray_fnbranch_minusro_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<T>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_roarray_fnbranch_minusro_kind_overload.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<T>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>
declare function isItemA(item: WritableDraft<Item>): item is WritableDraft<ItemA>
declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_roarray_fnbranch_minusro_setreq_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_roarray_fnbranch_minusro_setreq_kind_overload.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>
declare function isItemA(item: WritableDraft<Item>): item is WritableDraft<ItemA>
declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_roarray_fnbranch_setreq_kind.ts
type WritableDraft<T> = { [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_roarray_minusro_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<T>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_roarray_minusro_setreq_kind.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_roarray_setreq_kind.ts
type WritableDraft<T> = { [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: ReadonlyArray<Item> }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: property__this_setreq_kind.ts
type WritableDraft<T> = { [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>
interface Group { readonly items?: Item[] }
type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update: (state: Draft<this>) => void
  readonly group?: Group
  readonly kind: K
}
type ItemA = Item<'a'>

declare function isItemA(item: Item): item is ItemA
declare function useItemA(item: SetRequired<ItemA, 'group'>): void
export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) {
    useItemA(item)
    const p1: never = item
    const p2: never = item.update
    const p3: never = item.update.call
    const p4: never = item.update.length
    const p5: never = null! as keyof typeof item.update
    const p6: never = item.update.toString
    const p7: never = null! as keyof typeof item
    const p8: never = null! as Parameters<typeof item.update>
  }
}
// @filename: repro.ts
type WritableDraft<T> = { -readonly [P in keyof T]: Draft<T[P]> }
type Draft<T> = T extends Function ? T : T extends object ? WritableDraft<T> : T
type Simplify<T> = { [P in keyof T]: T[P] }
type SetRequired<T, P extends keyof T> = Simplify<Omit<T, P> & Required<Pick<T, P>>>

interface Group {
  readonly items?: ReadonlyArray<Item>
}

type Kind = 'a' | 'b'
declare class Item<K extends Kind = Kind> {
  update(state: Draft<this>): void
  readonly group?: Group
  readonly kind: K
}

type ItemA = Item<'a'>

declare function isItemA(item: WritableDraft<Item>): item is WritableDraft<ItemA>
declare function isItemA(item: Item): item is ItemA

declare function useItemA(item: SetRequired<ItemA, 'group'>): void

export function f(item: SetRequired<Item, 'group'>): void {
  if (isItemA(item)) useItemA(item)
}
// @filename: sentinel.ts
export const sentinel: number = "";
