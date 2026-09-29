// @strict: true
declare function take(a: number[]): void
declare function fa(a: any): void
export const f: any = { m() { take(this) } }
fa({ m() { take(this) } })
export const g = { m() { take(this) } } as any
export const h = ({ m() { take(this) } }) as any
export const i = { a: { m() { take(this) } } } as any
