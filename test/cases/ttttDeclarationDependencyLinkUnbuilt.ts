// The links tsgo knows (Program.GetSymlinkCache) include those of the runtime dependencies of every emitted file's
// package.json, found by package directory alone (resolvePackageDirectory): a workspace package whose entry isn't
// built still links its directory, so a file reached through it is named through the importing package's
// node_modules (crawlee's `../../node_modules/@crawlee/types/src/logger.js`)
// @declaration: true
// @module: commonjs
// @filename: /packages/types/package.json
{ "name": "@x/types", "version": "1.0.0", "types": "./dist/index.d.ts" }
// @filename: /packages/types/src/logger.ts
export interface Logger { log(message: string): void }
export declare function make(): Logger;
// @filename: /packages/core/package.json
{ "name": "@x/core", "version": "1.0.0", "dependencies": { "@x/types": "workspace:*" } }
// @filename: /packages/core/src/b.ts
export { make } from "../../types/src/logger";
// @filename: /packages/core/src/a.ts
import { make } from "./b";
export const logger = make();
// @link: /packages/types -> /packages/core/node_modules/@x/types
