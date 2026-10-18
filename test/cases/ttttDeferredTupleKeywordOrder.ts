// A tuple in an alias body is a deferred reference only when an element may resolve an alias: a built-in type
// (`object`) is a keyword in tsc, so `[N, object]` isn't, and a deferred reference sorts before an ordinary one
// of the same target in a union (compareNodes: a missing node last; vitest's reporter tuples)
// @strict: true
interface Opts { b: { x: 1 }; a: { y: 2 }; z: never; c: never }
type W<N extends string> = N extends keyof Opts ? Opts[N] extends never ? [N, object] : [N, Partial<Opts[N]>] : [N, Record<string, unknown>];
declare const u: unknown;
const r: W<keyof Opts>[] = [[u, {}]];
