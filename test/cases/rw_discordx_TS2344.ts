// origin: sourcesweep discordx (the root tsconfig, @types/node 25.9.5 and 26.1.2 both declaring "node:http2": http2.d.ts:2400-2482)
// flip: no
// A signature's type arguments are checked against their constraints with `this` the argument (tsgo's checkTypeArguments:
// getTypeWithThisArgument), unlike a type reference's: `typeof Resp<InstanceType<RQ>>` compares InstanceType<RQ>'s
// constraint (the other Req class, its `on()` returning itself) with Req whose `on()` returns InstanceType<RQ>, and fails.
// tttt left `this` alone there, and in getInstantiatedSignatures also checked the defaults.
// @strict: true
// @noEmit: true
// @filename: a.d.ts
declare module "m" {
    class Req { on(): this; }
    class Resp<R extends Req = Req> { readonly req: R; }
}
// @filename: b.d.ts
declare module "m" {
    class Req { on(): this; }
    class Resp<R extends Req = Req> { readonly req: R; }
    type T1<RQ extends typeof Req> = Resp<InstanceType<RQ>>;
    type T2<RQ extends Req> = Resp<RQ>;
    function g<RQ extends typeof Req>(): typeof Resp<InstanceType<RQ>>;
}
// @filename: c.ts
import { Req, Resp } from "m";
type T3<RQ extends typeof Req> = Resp<InstanceType<RQ>>;
