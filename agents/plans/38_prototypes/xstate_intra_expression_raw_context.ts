// @strict: true
// tsc: clean, `context: { count: number }`. The literal has two context-sensitive members, so it has intra-expression
// inference sites; tsc records them while checking the literal normally (the arrow's contextual signature comes from
// `invoke`'s contextual type instantiated with the non-fixing mapper: A = Actor, the first branch, a Mapper) and infers
// from them before the next fixing. tttt used to pre-walk the literal (infer_intra_expression()) and check each arrow
// against the *raw* member type, here the generic conditional's constraint, whose distributive branch has
// `input?: any`: no contextual signature, `({ context })` implicitly any (TS7031), and the parameter stayed that way.
// FIXED (plan 38 stage 2): sites are recorded by check_object()/check_array() (ty_add_intra_expression_site()).
// xstate: input.test.ts:255, invoke.test.ts:1427 (invoke: { src, input: ({ context }) => ... } next to a
// context factory).
interface Actor { src: string; logic: any }
type Mapper<C, R> = (args: { context: C }) => R;
type InvokeConfig<C, A extends Actor> = string extends A['src']
  ? { src: string; input?: Mapper<C, unknown> }
  : A extends Actor ? { src: A['src']; input?: Mapper<C, A['logic']> | A['logic'] } : never;
declare function createMachine<C, A extends Actor>(config: {
  types?: { context: C };
  context?: (args: { spawn: A }) => C;
  invoke?: InvokeConfig<C, A>;
}): [C, A];
const m = createMachine({
  types: {} as { context: { count: number } },
  context: (x) => ({ count: 1 }),
  invoke: { src: 'child', input: ({ context }) => context.count + 100 },
});
