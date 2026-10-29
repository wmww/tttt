// @strict: true
// @noEmit: true
// Deep nesting takes an indexed access's object type for its recursion identity the way it takes a type (tsc's
// getRecursionIdentityTarget): an instantiated mapped type unwrapped to its modifiers type, an intersection by its
// members. `Omit<Scope & {...}, "database">` against a type parameter goes through `(Scope & {...})[P]` -> `Input[P]`,
// `Scope` -> `Input`, `{...}[P]` -> `Input[P]`: the Pick's literal is on the source stack three times, the type
// parameter on the target's, so the comparable relation is a Maybe and the cast is fine (openclaw's
// session-transcript-worker-readers.ts). Without the Pick nothing nests and tsgo reports TS2352.
type Scope = Pick<{ agentId: string; other: number }, "agentId">;
type M = Omit<Scope & { kind: "c"; database: string }, "database">;
export function r<K extends "c", Input extends object>(kind: K, input: Input) {
  return { kind, ...input } as Extract<M, { kind: K }>;
}
type Plain = { agentId: string };
type N = Omit<Plain & { kind: "c"; database: string }, "database">;
export function s<K extends "c", Input extends object>(kind: K, input: Input) {
  return { kind, ...input } as Extract<N, { kind: K }>;
}
