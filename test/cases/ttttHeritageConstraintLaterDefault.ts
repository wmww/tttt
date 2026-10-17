// @strict: true
// A base class's type arguments are checked against constraints instantiated with the missing ones' defaults (tsc's
// fillMissingTypeArguments): `Input extends ClientInput` where a later `ClientInput extends object = any` isn't
// written is `Input extends any` (@aws-sdk's `extends $Command<XInput, XOutput, Config>`)
declare abstract class Command<Input extends ClientInput, Output, ClientInput extends object = any> {
    input: Input;
    output?: Output;
}
interface Req { a?: string }
interface CmdInput extends Req {}
export declare class C1 extends Command<CmdInput, number> {}
export declare class C2 extends Command<string, number> {}
declare abstract class Pair<A extends B, B extends string = "x"> { a: A; b: B }
export declare class P1 extends Pair<"x"> {}
export declare class P2 extends Pair<"y"> {}
const m = Command<CmdInput, number>;
