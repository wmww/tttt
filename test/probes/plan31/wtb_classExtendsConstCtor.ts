// @strict: true
// @filename: impl.d.ts
export interface CommandImpl<I> { input: I; }
// @filename: cmd.d.ts
import { CommandImpl } from "./impl";
interface In { Bucket: string; }
declare const Cmd_base: {
    new (input: In): import("./impl").CommandImpl<In>;
    new (input: In): import("./impl").CommandImpl<In>;
    getEndpointParameterInstructions(): number;
};
export declare class Cmd extends Cmd_base {
    protected static __types: { api: { input: In } };
}
// @filename: main.ts
import { Cmd } from "./cmd";
const c = new Cmd({ Bucket: "" });
c.input.Bucket;
declare class X { constructor(a: string); x: number }
declare const XBase: typeof X;
class Y extends XBase {}
new Y("");
new Y();
declare const G: { new <T = string>(a: T): { g: T } };
class Z extends G {}
new Z("");
new Z(1);
declare const R: { new <T>(a: T): { r: T }; new (a: number, b: number): { r: number } };
class W extends R {}
new W(1, 2);
new W(1);
