# Spelling suggestions are counted in source order, tsc counts them in check order

tsc gives at most 10 "Did you mean" suggestions per checker (`suggestionCount` / `maximumSuggestionCount` in
`resolveNameHelper`'s lazy diagnostic), and the count increases for every name it fails to resolve, suggestion or not.
The order is the order the *checker* resolves names: top-level statements first, then the deferred nodes (function
expression and arrow bodies, `checkNodeDeferred`) in FIFO order, a nested arrow appended behind its parent's queue.
tttt counts in the binder (`scope.c`, `binder->suggestion_count`), in source order.

Found by the source sweep: -TS2552 / +TS2593 / +TS2304 in 7 repos (jest, angular-components, mikro-orm, ...).
Reduced (jest `packages/jest-test-sequencer/src/__tests__/test_sequencer.test.ts`, `jest`, `expect`, `test` all
unresolved, `Test` an import of an unresolved module):

```ts
import type { Test } from "nope";
jest.mock("x", () => ({ a: jest.fn(), b: jest.fn(), c: jest.fn() }));  // 1 top-level, 3 deferred
const fs = jest.mocked(1);                                                // 2
beforeEach(() => { jest.clearAllMocks(); });                              // 3, deferred
test("a", () => { expect(1); });                                          // 4 (Did you mean 'Test'?), deferred
test("b", () => { expect(1); });                                          // 5
... (8 more top-level `test(...)` calls: tsc suggests 'Test' on all of the first 10 top-level misses)
```

tsc: every top-level `test` gets TS2552 up to the 10th miss overall *in check order*; tttt spends the count on the
deferred `jest.fn`/`expect` misses first and falls back to TS2593 for the later `test`s.

Fix direction: give each not-found event a check-order key (the chain of deferred-function nesting: top-level 0, the
n-th deferred function body by BFS) and count in that order when merging shard events (`resolve_shard_merge`
already replays events in an order). Not exact either: tsc also resolves names early when inference checks an arrow's
body (return type inference), but BFS by deferred function is the common case.
