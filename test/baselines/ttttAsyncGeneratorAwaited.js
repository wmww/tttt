//// [test/cases/ttttAsyncGeneratorAwaited.ts] ////

//// [ttttAsyncGeneratorAwaited.ts]
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


//// [ttttAsyncGeneratorAwaited.js]
// Async generators and iteration await generic types to `Awaited<T>` (tsc's getAwaitedType in createGeneratorType and
// the async iteration resolver, also for an iterator's own next()); returns of async functions and generators await
// without the alias (checkAwaitedType withAlias false, unwrapAwaitedType), and Awaited<T> relates to T
export async function* a(x) { const r = yield* { [Symbol.asyncIterator]: () => x }; return r; }
export async function* b(x) { const r = yield* x; return r; }
export async function* c(x) { const r = yield* { [Symbol.asyncIterator]: () => x }; return r; }
export async function* d(x) { const r = yield* x; return r; }
export async function* e(x) { yield* x; }
export async function* f(x) { yield x; }
export async function* g(x) { return x; }
export async function* h(x) { yield x; return x; }
export async function* i(x) { yield x; return x; }
export async function j(x) { for await (const v of x)
    return v; }
export async function k(x) { for await (const v of x)
    return v; }
export async function l(x) { for await (const v of x)
    return v; }
export async function m(x) { for await (const v of x)
    return v; }
export function n(x) { return x.next(); }
export async function* o(x) { yield x; return x; }
export async function* p(x) { return x; }
export async function* q(x) { return x; }
export async function r(x) { return x; }
export async function s(x) { return await x; }
function t1(x) { return aw(x); }
function t2(x) { return aw(x); }
function t3(x) { return aw(x); }
function t4(x) { return aw(x); }
function t5(x) { return x; }
function t6(x) { return x; }
async function u1(x) { return await x; }
async function u2(x) { const y = await x; return y; }
async function u3(x) { return await x; }
async function* u4(x) { yield await x; return await x; }
async function* u5(x) { return await x; }


//// [ttttAsyncGeneratorAwaited.d.ts]
export declare function a<T>(x: AsyncIterator<T, T>): AsyncGenerator<Awaited<T>, Awaited<T>, any>;
export declare function b<T>(x: AsyncIterableIterator<T, T>): AsyncGenerator<Awaited<T>, Awaited<T>, any>;
export declare function c<T>(x: {
    next(): Promise<IteratorResult<T, T>>;
}): AsyncGenerator<Awaited<T>, Awaited<T>, unknown>;
export declare function d<T>(x: Iterable<T>): AsyncGenerator<Awaited<T>, any, any>;
export declare function e<T>(x: T[]): AsyncGenerator<Awaited<T>, void, unknown>;
export declare function f<T>(x: Promise<T>): AsyncGenerator<Awaited<T>, void, unknown>;
export declare function g<T extends object>(x: T): AsyncGenerator<never, Awaited<T>, unknown>;
export declare function h<T extends string>(x: T): AsyncGenerator<T, T, unknown>;
export declare function i<T>(x: T | Promise<T>): AsyncGenerator<Awaited<T>, Awaited<T>, unknown>;
export declare function j<T>(x: AsyncIterable<T>): Promise<T | undefined>;
export declare function k<T>(x: Iterable<T | Promise<T>>): Promise<T | undefined>;
export declare function l<T>(x: {
    [Symbol.asyncIterator](): {
        next(): Promise<{
            done: false;
            value: T;
        }>;
    };
}): Promise<T | undefined>;
export declare function m<T>(x: {
    [Symbol.asyncIterator](): {
        next(): {
            done: false;
            value: T;
        };
    };
}): Promise<T | undefined>;
export declare function n<T>(x: AsyncGenerator<T, T>): Promise<IteratorResult<T, T>>;
export declare function o<T>(x: T): AsyncGenerator<Awaited<T>, Awaited<T>, unknown>;
export declare function p<T>(x: T): AsyncGenerator<never, Awaited<T>, unknown>;
export declare function q<T>(x: Promise<T>): AsyncGenerator<never, Awaited<T>, unknown>;
export declare function r<T>(x: T): Promise<T>;
export declare function s<T>(x: T): Promise<T>;
