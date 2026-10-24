// origin: foldkit/repos/effect packages/effect (source sweep), rpc/RpcClient.ts:417, :481, :504
// A conditional's branches that are generic functions are instantiated in the parameter's context before the
// conditional's union is made (tsc's checkExpression of each branch), so `c ? ws2 : identity` infers like `ws2` alone
// @strict: true
interface Pipeable {
    pipe<A>(this: A): A;
    pipe<A, B = never>(this: A, ab: (_: A) => B): B;
    pipe<A, B = never, C = never>(this: A, ab: (_: A) => B, bc: (_: B) => C): C;
}
interface Eff<out A, out E, out R> extends Pipeable { a: () => A; e: () => E; r: () => R }
declare function identity<T>(a: T): T;
declare const send: Eff<void, string, never>;
declare const c: boolean;
declare const ws: <A, E, R>(self: Eff<A, E, R>) => Eff<A, E, R>;
declare const ws2: <A, E, R>(self: Eff<A, E, R>) => Eff<A, E, Exclude<R, number>>;
const b1 = send.pipe(c ? ws : identity);
const b2 = send.pipe(c ? ws2 : identity);
