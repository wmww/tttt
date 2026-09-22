// @strict: true
type Args = (string | number)[];
type MsgAndArgs = [message: { key: string }, ...args: Args];
declare function report(message: { key: string }, ...args: Args): void;
declare const m: MsgAndArgs;
report(...m);
declare const stack: MsgAndArgs[];
report(...stack[0]);
