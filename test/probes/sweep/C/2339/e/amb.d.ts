declare module "node:assert/strict" {
    import { strict } from "node:assert";
    export = strict;
}
declare module "node:assert" {
    import strict = require("node:assert/strict");
    function assert(value: unknown): asserts value;
    namespace assert {
        function ok(value: unknown): void;
        const strict: Omit<typeof assert, "strict"> & { (value: unknown): asserts value };
    }
    namespace assert { export { strict }; }
    export = assert;
}
declare module "assert" {
    import assert = require("node:assert");
    export = assert;
}
