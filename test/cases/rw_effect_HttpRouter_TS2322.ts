// origin: foldkit/repos/effect packages/effect (source sweep), http/HttpRouter.ts:1219 and :1245
// A generic function argument is instantiated in the context of an intersection parameter type whose generic member
// has no signatures (tsc's getSignaturesOfType through the apparent type): E and R are inferred from its return
// @strict: true
interface Eff<A, E, R> { a: () => A; e: () => E; r: () => R }
interface unhandled { readonly _: unique symbol }
type NoInfer2<T> = [T][T extends any ? 0 : never];
declare function make<E, R>(
    mw: ((effect: Eff<string, NoInfer<unhandled>, NoInfer<never>>) => Eff<string, E, R>)
        & (unhandled extends E ? unknown : "You must only handle the configured errors")
): [E, R];
declare function make2<E, R>(
    mw: ((effect: Eff<string, NoInfer<unhandled>, NoInfer<never>>) => Eff<string, E, R>)
): [E, R];
declare const withLoggerDisabled: <A, E, R>(self: Eff<A, E, R>) => Eff<A, E, R | number>;
const r1 = make(withLoggerDisabled);
const r2 = make2(withLoggerDisabled);
