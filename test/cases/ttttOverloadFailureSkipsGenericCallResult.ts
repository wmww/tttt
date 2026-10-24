// @strict: true
// @target: esnext
// An overload failure's fallback inference (SkipGenericFunctions) skips an argument that is a call returning a generic
// function, like any generic function argument (tsc's anyFunctionType): the failed pipe's C stays its default, never
interface Box<A, R> {
    a: A;
    r: () => R;
    pipe<X, B = never, C = never>(this: X, ab: (_: X) => B, bc: (_: B) => C): C;
    [Symbol.iterator](): Iterator<Box<A, R>, A, any>;
}
interface Ctx<R> { r: R }
declare function sat<R>(): <A, R2 extends R>(e: Box<A, R2>) => Box<A, R2>;
declare function provide<R>(ctx: Ctx<R>): <A>(self: Box<A, R>) => Box<A, never>;
declare const b: Box<string, unknown>;
declare const empty: Ctx<never>;

const viaCall = b.pipe(sat<never>(), provide(empty));
const n: number = viaCall;

function* gen() {
    yield* b.pipe(sat<never>(), provide(empty));
}
