// origin: n8n-io/n8n at 3c7a553dc packages/cli (named projects, seed 20269001), agent-execution.service.test.ts:1911 and
// agent-background-job.service.test.ts:380: vitest's `it.each<Row>([...])(name, ({ a, b }) => ...)` picks the overload
// whose callback is `(...args: T) => ...` with T an object. The contextual parameter at position 0 is `T[0]`, which
// tsc's getIndexedAccessType makes unknown without an access node, so a destructured parameter falls back to its
// pattern's type (no TS7031) and a plain one is unknown (no TS7006).
// @strict: true
declare function each<T extends any[] | [any]>(cases: ReadonlyArray<T>): (name: string, fn: (...args: T) => void) => void;
each<{ name: string; allowed: boolean }>([{ name: "a", allowed: true }])("x", ({ name, allowed }) => {});
declare function rest<T>(fn: (...args: T) => void): void;
rest<{ a: number }>(({ a }) => {});
rest<{ a: number }>((x) => {});
rest<{ a: number }>(([first]) => {});
rest<{ 0: string; a: number }>((x, y) => {});
rest<string>((c) => {});
