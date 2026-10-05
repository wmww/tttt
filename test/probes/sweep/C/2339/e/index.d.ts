import assert = require("assert");
declare module "assert" {
    function file(path: string): void;
}
export = assert;
