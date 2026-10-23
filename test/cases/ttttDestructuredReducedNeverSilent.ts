// @strict: true
// A property destructured from an intersection reduced to never is never, without an error: tsc's
// getPropertyTypeForIndexType returns the reduced apparent never silently; only a property access expression reports
// TS2339 (checkPropertyAccessExpression). portabase's `const { status } = await auth.api.unlinkAccount(...)`, a
// better-auth response whose `status` is number in one member and boolean in another
declare const x: { status: number; a: 1 } & { status: boolean }
const p = x.status
const e = x["status"]
const { status: s, a } = x
let t: unknown
;({ status: t } = x)
declare const u: ({ k: 1 } & { k: 2 }) | ({ k: 3 } & { k: 4 })
const { k } = u
const k2 = u["k"]
const k3 = u.k
function f({ status: s2 }: { status: number } & { status: boolean }) {}
