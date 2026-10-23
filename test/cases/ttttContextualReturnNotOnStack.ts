// @strict: true
// IQux is first asked inside Qux's call (tsc checks the alias's type arguments before the reference): the call's
// inference reaches QuxRepository's base, IQux, typeof Qux, and Qux again below the call's barrier. The nested
// resolution of the same call asks for `() => QuxRepository`'s return type, which the outer contextual check is
// inferring: tsc's contextuallyCheckFunctionExpressionOrObjectLiteralMethod does that without pushTypeResolution, so
// it is inferred again rather than a cycle (TS2456/TS7022/TS7023 otherwise; mikro-orm tests/defineEntity.test.ts)
declare class EntityRepository<T> { entity: T }
type Constructor<T> = new (...args: any[]) => T
declare function defineEntity<P, R = never>(meta: {
  name: string
  repository?: () => R
  properties: P
}): { properties: P; repository: R }
type InferEntity<S> = S extends { properties: infer P } ? P : never

export function test() {
  type IQux = InferEntity<typeof Qux>

  class QuxRepository extends EntityRepository<IQux> {
    another(): string { return "x" }
  }

  const Qux = defineEntity({
    name: "Qux",
    repository: () => QuxRepository,
    properties: { id: 1, title: "" },
  })
  return Qux
}
