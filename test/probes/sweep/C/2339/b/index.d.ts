import Command = require("./cmd");
declare namespace GulpRunner {
    export { Command };
    export interface Options { x?: number }
}
declare function GulpRunner(s: string): void;
export = GulpRunner;
