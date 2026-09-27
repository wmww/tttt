// @strict: true
type Awaitable<T> = T | PromiseLike<T>;
type ExtractEachCallbackArgs<T extends ReadonlyArray<any>> = {
	1: [T[0]];
	2: [T[0], T[1]];
	3: [T[0], T[1], T[2]];
	fallback: Array<T extends ReadonlyArray<infer U> ? U : any>;
}[T extends Readonly<[any]> ? 1 : T extends Readonly<[any, any]> ? 2 : T extends Readonly<[any, any, any]> ? 3 : "fallback"];
interface EachFunctionReturn<T extends any[]> {
	(name: string | Function, fn: (...args: T) => Awaitable<void>, options?: number): void;
	(name: string | Function, options: {}, fn: (...args: T) => Awaitable<void>): void;
}
interface TestEachFunction {
	<T extends any[] | [any]>(cases: ReadonlyArray<T>): EachFunctionReturn<T>;
	<T extends ReadonlyArray<any>>(cases: ReadonlyArray<T>): EachFunctionReturn<ExtractEachCallbackArgs<T>>;
	<T>(cases: ReadonlyArray<T>): EachFunctionReturn<T[]>;
}
declare const each: TestEachFunction;
declare function keysOf(s: string): string[];
const asFormat = (k: string) => ({ code: "invalid_format", format: k });
const asSize = (k: string) => ({ code: "too_small", origin: k, minimum: 2 });
const r = each([
  ["FormatDictionary", keysOf("FormatDictionary"), asFormat],
  ["Sizable", keysOf("Sizable"), asSize],
] as const);
r("every", (_block, keys, build) => {
  const required = keys.filter((k) => build(k));
});
