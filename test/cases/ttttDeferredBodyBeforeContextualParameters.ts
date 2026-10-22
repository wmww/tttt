// @strict: true
// @target: es2015
// Known failing (bugs/missing_checks.md "Binding elements read before the contextual assignment"). A generic call
// failing at an argument before a context-sensitive callback: tsgo's inference pass queues the callback's deferred
// body check (checkNodeDeferred, SkipContextSensitive), every candidate fails earlier so nothing context-checks it, and
// the call's own deferred check (resolveUntypedCall, which assigns the parameter its pattern type `{ b: any }`) comes
// after. The body reads `b` first: typed from the raw contextual `unknown`, TS2339. tttt always takes the pattern
// fallback before any element is typed. (The arrayFrom return-void and destructuringWithGenericParameter null-arg
// mutants.)
declare function from<T, U>(iterable: Iterable<T> | ArrayLike<T>, mapfn: (v: T, k: number) => U): U[];
declare const v: void;
interface B { b: string }
const r1: B[] = from(v, ({ a }): B => ({ b: a }));

class GenericClass<T> { payload!: T; }
declare function genericFunction<T>(object: GenericClass<T>, callback: (payload: T) => void): void;
genericFunction(null, ({ greeting }) => {
    var s = greeting.toLocaleLowerCase();
});
// Not read by the body itself (only by a nested arrow, checked after the call): no error in tsgo either
genericFunction(null, ({ greeting }) => () => greeting);
genericFunction(null, ({ greeting }) => {});
