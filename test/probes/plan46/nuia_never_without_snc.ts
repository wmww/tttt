// noUncheckedIndexedAccess without strictNullChecks: an index signature's never is undefined (never | undefined), and an
// undefined callee is TS2722 (drizzle mysql-proxy/migrator.ts 25 under flagflip's sloppy/nosnc: `.limit` of an Omit<never, K>)
// @strictNullChecks: false,true
// @noUncheckedIndexedAccess: true
declare const o: { [k: string]: never }
export const a = o.limit
export const a2 = o.limit(1)
export const b = o["x"]
declare const p: { [k: string]: string }
export const c = p.limit
