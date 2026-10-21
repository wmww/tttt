// @target: es2015
// Imports and exports in a function or block (TS1232/TS1233) are never resolved by their statement's check, but the
// spelling suggestion of an unresolved name looks at every alias in scope and resolves it: what that resolution says
// (TS2307, TS2305) is reported then, and only then.
function f() {
    declare module "nested" {
        export const a: number;
    }
    import * as Foo from "nested";
    import { baz } from "nested";
    import I2 = require("missing");
    export { qux as q } from "nested";
    missingName;
}
function g() {
    // Nothing unresolved here: nothing resolves these.
    import * as Foo from "nested2";
    import { baz } from "nested2";
}
{
    declare module "ambient" {
        export const a: number;
    }
    import { baz } from "ambient";
    export { qux as q } from "ambient";
    otherMissingName;
}
