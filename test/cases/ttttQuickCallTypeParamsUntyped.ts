// origin: npm SII-Holos/synergy @ a283cf3 packages/util/src/features.ts:43 (seen from packages/harness and local-runtime, tsconfig.bench.json)
// flip: yes
// The quick type of a call of a function declared with a return type is that type, its parameters untyped (tsc's
// getSignatureFromDeclaration): typing activate's parameter here reached back to Plan's initializer, a cycle
// @strict: true
interface Schema<T> {
  refine<Ch extends (arg: T) => unknown | Promise<unknown>>(check: Ch, msg?: string): Ch extends (arg: any) => arg is infer R ? this & Schema<R> : this;
  _out: T
}
declare function object<T>(shape: T): Schema<T>;
type Infer<S> = S extends { _out: infer T } ? T : never;

export const Plan = object({ ids: [] as string[], names: [] as string[] })
  .refine((plan) => {
    const active = activate(plan)
    return plan.ids.length === active.size
  }, "mismatch")
export type Plan = Infer<typeof Plan>

function activate(plan: Pick<Plan, "ids">): Set<string> {
  return new Set(plan.ids)
}
