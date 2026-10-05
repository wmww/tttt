import { Primitive } from "lib";
declare module "lib" {
    interface Tooltip { x: Primitive }
    function tip(): Tooltip;
}
