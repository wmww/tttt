//// [test/cases/ttttDeclarationDependencyLinkUnbuilt.ts] ////

//// [package.json]
{ "name": "@x/types", "version": "1.0.0", "types": "./dist/index.d.ts" }
//// [logger.ts]
export interface Logger { log(message: string): void }
export declare function make(): Logger;
//// [package.json]
{ "name": "@x/core", "version": "1.0.0", "dependencies": { "@x/types": "workspace:*" } }
//// [b.ts]
export { make } from "../../types/src/logger";
//// [a.ts]
import { make } from "./b";
export const logger = make();


//// [logger.js]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
//// [b.js]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.make = void 0;
var logger_1 = require("../../types/src/logger");
Object.defineProperty(exports, "make", { enumerable: true, get: function () { return logger_1.make; } });
//// [a.js]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.logger = void 0;
const b_1 = require("./b");
exports.logger = (0, b_1.make)();


//// [logger.d.ts]
export interface Logger {
    log(message: string): void;
}
export declare function make(): Logger;
//// [b.d.ts]
export { make } from "../../types/src/logger";
//// [a.d.ts]
export declare const logger: import("@x/types/src/logger").Logger;
