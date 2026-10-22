// @strict: true
// An arrow's return type in flight asked again from below a call's resolution isn't circular: tsc's
// getReturnTypeOfSignature pushes from resolutionStart, which getResolvedSignature resets, so it infers it again,
// nested. Here: `state`'s type reads `result` in the loop, whose back edge is `wrap(() => action(state))`, whose
// inference asks the arrow's return type while the outer inference of it (through action(state) -> state) is in
// flight. tttt found the signature return type circular, inferred T from the return context alone (`R | undefined`)
// and reported `result` possibly undefined (angular-cli build-action.ts). Known failing types: `state` prints any (the
// nested evaluation of `result.make` re-enters the loop's flow until lazy_type.c's 8-hit expression bail, tsc's flow
// loop finds itself in progress: bugs/missing_checks.md "An expression's nested runaway bail")
interface R { make(c: string): number; }
declare function wrap<T>(action: () => T): T;
declare function action(s: number): R;
declare const cond: boolean;
export function run() {
  let result: R | undefined;
  result = action(0);
  while (cond) {
    const state = result.make("");
    result = wrap(() => action(state));
  }
}
