// @strict: true
// xstate setup({ guards: { check: (_, params: number) => true } }): `_` is implicitly any in tttt (TS7006/TS7031,
// ~6 diagnostics in setup.types.test.ts: lines 37, 48, 98, 110, 120, 132). tsc: `_: { context: string }`, no errors.
// In the second pass TGuards has no candidates. tsc's instantiateContextualType only replaces instantiable types at
// the top level (instantiateInstantiableTypes), so the contextual type stays the generic mapped type and its `check`
// property is GP<TGuards["check"]>. tttt instantiates the whole parameter type with the inferences so far, where
// TGuards is its default `{}`: the mapped type becomes `{}`, `check` has no contextual type. Its "only where it
// would be unknown" stand-in covers `r3` (no default) but not a default. Measured: also keeping type parameters
// with a default and no candidates as themselves is suite-neutral and xstate 238 -> 232 (not committed: plan 38).
type GP<TParams> = (args: { context: string }, params: TParams) => boolean;
declare function setup<TGuards extends Record<string, unknown> = {}>(cfg: {
  guards?: { [K in keyof TGuards]: GP<TGuards[K]> };
}): TGuards;
const r = setup({ guards: { check: (_, params: number) => true } }); // tsc: r: { check: number }

declare function setup3<TGuards extends Record<string, unknown>>(cfg: {
  guards?: { [K in keyof TGuards]: GP<TGuards[K]> };
}): TGuards;
const r3 = setup3({ guards: { check: (_, params: number) => true } }); // fine in both
