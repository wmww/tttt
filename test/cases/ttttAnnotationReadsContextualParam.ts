// @strict: true
// A context-sensitive function's annotations read while a generic call infers from them (tsc's
// inferFromAnnotatedParametersAndReturn: `(x): typeof x => x`) type an unannotated parameter they mention by the
// contextual signature they are lowered against (getContextuallyTypedParameterType finds the argument's contextual
// type): no TS7006. openclaw's post-core-dependency-health.integration.test.ts
// `payloads.map((payload): [typeof payload, number | null | undefined] => [payload, undefined])`
declare function g<U>(cb: (v: string) => U): U;
const a = g((x): typeof x => x);
const payloads = ["missing", "hoisted"] as const;
const r = payloads.map((payload): [typeof payload, number] => [payload, 1]);
