// An import of the file already in the importing file names it (computeModuleSpecifiers' existingSpecifier) only if
// what it resolved to, the realpath, is one of the paths the file is named by: in pnpm's store it isn't when a link to
// it is known (containsIgnoredPath), and the specifier is generated anew (mcp-sdk's tsdown.config.d.ts)
// @declaration: true
// @module: commonjs
// @filename: /node_modules/.pnpm/lib@1.0.0/node_modules/lib/package.json
{ "name": "lib", "version": "1.0.0", "types": "index.d.ts" }
// @filename: /node_modules/.pnpm/lib@1.0.0/node_modules/lib/index.d.ts
export interface Opts { a: number }
export declare function make(): Opts;
// @filename: /a.ts
import { make } from "lib/index";
export const x = make();
// @link: /node_modules/.pnpm/lib@1.0.0/node_modules/lib -> /node_modules/lib
