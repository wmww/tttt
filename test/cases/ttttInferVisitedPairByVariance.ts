// @strict: true
// An inference walk visits a (source, target) pair once per mode, its priority and variances being part of the key
// (tsgo's InferenceKey): `[A]` to `AsArray<T>` met in `promise`'s parameter (contravariant) is walked again in `tap`'s
// callback parameter (covariant), whose candidates `A` and `B` (the conditional's false branch unwraps the tuple)
// reduce the union to `B`. With one walk per pair the inferred T was `[A]` (webpack's tapable hooks)
type AsArray<T> = T extends any[] ? T : [T];
interface Interceptor<T, R> {}
declare class Hook<T, R> {
	intercept(interceptor: Interceptor<T, R>): void;
	promise(...args: AsArray<T>): Promise<R>;
	tap(fn: (...args: AsArray<T>) => R): void;
}
type A = { a?: string };
type B = {};
const f: () => Hook<[A], number> & Hook<[B], string> = () => new Hook();
