// @strict: true
// xstate invoke.test.ts: `invoke: { src, onDone: { actions: assign({ count: ({ event }) => event.output.count }) } }`.
// onDone's `actions` is contextually a union (InvokeConfig's conditional has DoneActorEvent<any> in one branch and
// DistributeActors' DoneActorEvent<unknown> in the other, both reach the constraint union), so inferring assign's
// TExpressionEvent from its return type gives two contravariant candidates at priority ReturnType. tsc intersects
// them (getContravariantInference: PriorityImpliesCombination = ReturnType | MappedTypeConstraint | LiteralKeyof):
// event is `Done<any> & Done<unknown>`, output `any`, no errors. tttt only intersected at LiteralKeyof and took the
// common subtype `Done<unknown>`: TS18046 'event.output' is of type 'unknown' (4 xstate diagnostics; the
// onSnapshot / `Snapshot<unknown>` TS2339s are the same mechanism).
interface Done<T> { type: string; output: T }
declare function assign<TE>(a: (args: { event: TE }) => number): (args: { event: TE }) => void;
declare function take(action: ((args: { event: Done<any> }) => void) | ((args: { event: Done<unknown> }) => void)): void;
take(assign(({ event }) => event.output.count));
