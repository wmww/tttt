// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "es2022", "lib": ["es2022"], "types": []}, "files": ["date_fn_map__draft__notlocked__and_obj__locked__as__empty_if.ts", "date_fn_map__draft__notlocked__omit_and__locked__as__empty_if.ts", "date_fn_map__draft__notlocked__pick_and__locked__as__empty_if.ts", "date_fn_map__draft__notlocked__setreq__locked__as__and.ts", "date_fn_map__draft__notlocked__setreq__locked__as__early.ts", "date_fn_map__draft__notlocked__setreq__locked__as__in_if.ts", "date_fn_map__draft__notlocked__setreq__locked__as__ternary.ts", "date_fn_map__draft__notlocked__setreq__locked__call__empty_if.ts", "date_fn_map__draft__notlocked__setreq__locked__let__empty_if.ts", "date_fn_map__draft__notlocked__setreq__locked__prop__empty_if.ts", "date_fn_map__draft__notlocked__setreq__locked_fn__as__empty_if.ts", "date_fn_map__draft__notlocked__setreq__plain__as__empty_if.ts", "date_fn_map__draft_ret__notlocked__setreq__locked__as__empty_if.ts", "date_fn_map__keys__notlocked__setreq__locked__as__empty_if.ts", "date_fn_map__none__notlocked__setreq__locked__as__empty_if.ts", "date_fn_map__partial__notlocked__setreq__locked__as__empty_if.ts", "date_fn_map__this__notlocked__setreq__locked__as__empty_if.ts", "fn__draft__cond__setreq__locked__as__empty_if.ts", "fn_date__draft__cond__setreq__locked__as__early.ts", "fn_date__draft__draft__setreq__locked__as__early.ts", "fn_date__draft__notlocked__setreq__locked__param__and.ts", "fn_date__draft__this__setreq__locked__as__early.ts", "fn_date__draft__void__setreq__locked__as__early.ts", "fn_regexp__draft__cond__setreq__locked__as__empty_if.ts", "report.ts"]}
// @filename: date_fn_map__draft__notlocked__and_obj__locked__as__empty_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is Customer & { readonly referredBy: number }
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__draft__notlocked__omit_and__locked__as__empty_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is Omit<Customer, 'referredBy'> & { readonly referredBy: number }
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__draft__notlocked__pick_and__locked__as__empty_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is Pick<Customer, 'merge'> & { readonly referredBy: number }
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__draft__notlocked__setreq__locked__as__and.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  const b = isReferredCustomer(customer) && customer.merge
  const p1: never = b
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__draft__notlocked__setreq__locked__as__early.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (!isReferredCustomer(customer)) return customer
  const p1: never = customer.merge
  const p2: never = null! as Parameters<typeof customer.merge>[0]
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__draft__notlocked__setreq__locked__as__in_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (isReferredCustomer(customer)) {
    const p1: never = customer
    const p2: never = customer.merge
    const p3: never = customer.referredBy
    return customer
  }
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__draft__notlocked__setreq__locked__as__ternary.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  const t = isReferredCustomer(customer) ? customer : customer
  const p1: never = t
  return t
}
export const sentinel: number = ""
// @filename: date_fn_map__draft__notlocked__setreq__locked__call__empty_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
declare function get(): Locked<Customer>
export function f() {
  const customer = get()
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__draft__notlocked__setreq__locked__let__empty_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
declare let outer: Locked<Customer>
export function f() {
  const customer = outer
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__draft__notlocked__setreq__locked__prop__empty_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(o: { readonly c: Locked<Customer> }) {
  const customer = o.c
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__draft__notlocked__setreq__locked_fn__as__empty_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer> & { (): void }
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__draft__notlocked__setreq__plain__as__empty_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Customer
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__draft_ret__notlocked__setreq__locked__as__empty_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => Draft<this>): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__keys__notlocked__setreq__locked__as__empty_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (k: keyof this) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__none__notlocked__setreq__locked__as__empty_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: () => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__partial__notlocked__setreq__locked__as__empty_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Partial<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: date_fn_map__this__notlocked__setreq__locked__as__empty_if.ts
type AtomicObject = Date | Function | Map<any, any>
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: this) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: fn__draft__cond__setreq__locked__as__empty_if.ts
type AtomicObject = Function
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): this extends { referredBy: number } ? 1 : 2
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: fn_date__draft__cond__setreq__locked__as__early.ts
type AtomicObject = Function | Date
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): this extends { referredBy: number } ? 1 : 2
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (!isReferredCustomer(customer)) return customer
  const p1: never = customer.merge
  const p2: never = null! as Parameters<typeof customer.merge>[0]
  return customer
}
export const sentinel: number = ""
// @filename: fn_date__draft__draft__setreq__locked__as__early.ts
type AtomicObject = Function | Date
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): Draft<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (!isReferredCustomer(customer)) return customer
  const p1: never = customer.merge
  const p2: never = null! as Parameters<typeof customer.merge>[0]
  return customer
}
export const sentinel: number = ""
// @filename: fn_date__draft__notlocked__setreq__locked__param__and.ts
type AtomicObject = Function | Date
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(customer: Locked<Customer>) {
  const b = isReferredCustomer(customer) && customer.merge
  const p1: never = b
  return customer
}
export const sentinel: number = ""
// @filename: fn_date__draft__this__setreq__locked__as__early.ts
type AtomicObject = Function | Date
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): this
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (!isReferredCustomer(customer)) return customer
  const p1: never = customer.merge
  const p2: never = null! as Parameters<typeof customer.merge>[0]
  return customer
}
export const sentinel: number = ""
// @filename: fn_date__draft__void__setreq__locked__as__early.ts
type AtomicObject = Function | Date
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): void
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (!isReferredCustomer(customer)) return customer
  const p1: never = customer.merge
  const p2: never = null! as Parameters<typeof customer.merge>[0]
  return customer
}
export const sentinel: number = ""
// @filename: fn_regexp__draft__cond__setreq__locked__as__empty_if.ts
type AtomicObject = Function | RegExp
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject ? T : T extends object ? WritableDraft<T> : T
type SetRequiredNonNullable<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>> & { [P in K]-?: NonNullable<T[P]> }
declare class LockedTag { private readonly locked: true }
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T
declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): this extends { referredBy: number } ? 1 : 2
  readonly referredBy: number | null
}
declare function isReferredCustomer(customer: Customer): customer is SetRequiredNonNullable<Customer, 'referredBy'>
export function f(x: unknown) {
  const customer = x as Locked<Customer>
  if (isReferredCustomer(customer)) {
  }
  const p0: never = customer
  return customer
}
export const sentinel: number = ""
// @filename: report.ts
type AtomicObject = Function | Date
type WritableDraft<T> = { -readonly [K in keyof T]: Draft<T[K]> }
type Draft<T> = T extends AtomicObject
  ? T
  : T extends object
    ? WritableDraft<T>
    : T

type SetRequiredNonNullable<T, K extends keyof T> = Pick<
  T,
  Exclude<keyof T, K>
> & { [P in K]-?: NonNullable<T[P]> }

declare class LockedTag {
  private readonly locked: true
}
type Locked<T> = T & LockedTag
type NotLocked<T> = T extends Locked<infer U> ? U : T

declare class Customer {
  merge(produceFn: (state: Draft<this>) => void): NotLocked<this>
  readonly referredBy: number | null
}

declare function isReferredCustomer(
  customer: Customer,
): customer is SetRequiredNonNullable<Customer, 'referredBy'>

export function f(x: unknown): Locked<Customer> {
  const customer = x as Locked<Customer>
  if (isReferredCustomer(customer)) {
  }
  return customer
}
