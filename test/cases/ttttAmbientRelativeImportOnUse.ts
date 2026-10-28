// An import of a relative name in an ambient module is TS2439, and its bindings aren't checked: its module is resolved
// (TS2307) only by a use of the alias, as a type, a value, a qualified name or an export specifier
declare module "OuterModule" {
    import unused = require("./Unused");
    import { unusedNamed } from "./UnusedNamed";
    import m2 = require("./SubModule");
    import { a } from "./Other";
    import d from "./Def";
    import { e } from "./Eee";
    import "./Side";
    export const x: a;
    export const y: typeof m2;
    export const z: typeof d;
    export const w: e.f;
}
declare module "Other2" {
    import { a } from "./Other";
    import { b } from "./Other";
    export { a };
}
