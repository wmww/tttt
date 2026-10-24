// A workspace package's `.ts` file imported by a non-relative specifier through its node_modules link is a file found
// searching node_modules (not emitted, tsc's IsSourceFileFromExternalLibrary), even though its real path has no
// node_modules in it: no TS2877 under rewriteRelativeImportExtensions (akash console's tsup.config.ts, latitude-llm)
// @filename: /apps/api/tsconfig.json
{ "compilerOptions": { "noEmit": true, "module": "esnext", "moduleResolution": "bundler", "allowImportingTsExtensions": true, "rewriteRelativeImportExtensions": true, "types": [] }, "include": ["*.ts"] }
// @filename: /packages/dev-config/package.json
{ "name": "@x/dev-config", "version": "1.0.0" }
// @filename: /packages/dev-config/tsup-plugins.ts
export const u = 1;
// @filename: /apps/api/package.json
{ "name": "api" }
// @filename: /apps/api/tsup.config.ts
import { u } from "@x/dev-config/tsup-plugins.ts";
export const v = u;
// @link: /packages/dev-config -> /apps/api/node_modules/@x/dev-config
