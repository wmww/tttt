// @strict: true
// A call statement's effects (tsc's getEffectsSignature) are read from the callee's declaration: a function with an
// annotated return type that is neither a type predicate nor never has none, and its parameter types aren't needed.
// Typing them here would reach the alias whose function's return type is being inferred (openclaw's
// update-command-terminal-record.ts: a spurious TS2456, TS7023 and TS2502)
export type Rec = NonNullable<Awaited<ReturnType<typeof capture>>>;
export async function capture(flag: boolean) {
  if (!flag) return undefined;
  const captured = { run: 1, path: "p" };
  check(captured);
  return captured;
}
export function check(captured: Rec): string {
  return captured.path;
}
// A `never` alias still ends the flow, an assertion still narrows
type Never = never;
declare function fail(message: string): Never;
declare function assertString(x: unknown): asserts x is string;
export function f(x: unknown, y: number | undefined) {
  if (y === undefined) {
    fail("no");
  }
  assertString(x);
  return [x, y] as const;
}
