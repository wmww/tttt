// @target: es2018
// @strict: true
// @declaration: true
// Async generators and iteration await generic types to `Awaited<T>` (tsc's getAwaitedType in createGeneratorType and
// the async iteration resolver, also for an iterator's own next()); returns of async functions and generators await
// without the alias (checkAwaitedType withAlias false, unwrapAwaitedType), and Awaited<T> relates to T
export async function* a<T>(x: AsyncIterator<T, T>) { const r = yield* { [Symbol.asyncIterator]: () => x }; return r; }
export async function* b<T>(x: AsyncIterableIterator<T, T>) { const r = yield* x; return r; }
export async function* c<T>(x: { next(): Promise<IteratorResult<T, T>> }) { const r = yield* { [Symbol.asyncIterator]: () => x }; return r; }
export async function* d<T>(x: Iterable<T>) { const r = yield* x; return r; }
export async function* e<T>(x: T[]) { yield* x; }
export async function* f<T>(x: Promise<T>) { yield x; }
export async function* g<T extends object>(x: T) { return x; }
export async function* h<T extends string>(x: T) { yield x; return x; }
export async function* i<T>(x: T | Promise<T>) { yield x; return x; }
export async function j<T>(x: AsyncIterable<T>) { for await (const v of x) return v; }
export async function k<T>(x: Iterable<T | Promise<T>>) { for await (const v of x) return v; }
export async function l<T>(x: { [Symbol.asyncIterator](): { next(): Promise<{ done: false, value: T }> } }) { for await (const v of x) return v; }
export async function m<T>(x: { [Symbol.asyncIterator](): { next(): { done: false, value: T } } }) { for await (const v of x) return v; }
export function n<T>(x: AsyncGenerator<T, T>) { return x.next(); }
export async function* o<T>(x: T) { yield x; return x; }
export async function* p<T>(x: T) { return x; }
export async function* q<T>(x: Promise<T>) { return x; }
export async function r<T>(x: T) { return x; }
export async function s<T>(x: T) { return await x; }
declare function aw<T>(x: T): Awaited<T>;
function t1<T>(x: T): T { return aw(x); }
function t2<T extends object>(x: T): T { return aw(x); }
function t3<T extends Promise<number>>(x: T): T { return aw(x); }
function t4<T, U>(x: T | U): T { return aw(x); }
function t5<T>(x: Awaited<T>): number { return x; }
function t6<T extends number>(x: Awaited<T>): number { return x; }
async function u1<T>(x: T): Promise<T> { return await x; }
async function u2<T extends object>(x: T): Promise<T> { const y = await x; return y; }
async function u3<T>(x: T): Promise<string> { return await x; }
async function* u4<T>(x: T): AsyncGenerator<T, T> { yield await x; return await x; }
async function* u5<T>(x: T): AsyncGenerator<T, string> { return await x; }
