// @strict: true
// `cache?.delete(path);` is a statement: narrowing `path` by its optional-chain condition reads the call's effects
// through the callee's declared type (tsc's getEffectsSignature: getTypeOfDottedName when the parent is an expression
// statement), not its flow type, which here would narrow `cache` by `cache?.get(path) !== prior` and so need the type
// of `prior` while it is being inferred (openclaw's agent-tools.read.ts, a spurious TS7022)
export function f(cache: Map<string, number> | undefined, path: string | undefined, content?: Set<string>) {
  if (!path || !content?.has(path)) return;
  for (;;) {
    const prior = cache?.get(path);
    if (cache?.get(path) !== prior) {
    }
    cache?.delete(path);
  }
}
// An assertion called as an optional statement still narrows
declare const checker: { assertString(x: unknown): asserts x is string } | undefined;
export function g(x: unknown) {
  checker?.assertString(x);
  return x;
}
