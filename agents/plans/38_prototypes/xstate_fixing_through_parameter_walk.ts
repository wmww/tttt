// @strict: true
// @target: es2015
// tsc: p is Box<unknown>. Inferring from the callback's parameters walks `self: Box<T>`, which fixes T before the
// return type is seen. tttt's fixing gate decides `({ sys })` doesn't fix T, types the parameter Box<unknown> and still
// infers T = number. xstate: actorLogic.test.ts x7, predictableExec, system (TS2345).
interface Box<T> { get(): T; set(v: T): void }
declare function fromPromise<T>(f: (a: { sys: string; self: Box<T> }) => Promise<T>): Box<T>;
const p = fromPromise(({ sys }) => Promise.resolve(42));
