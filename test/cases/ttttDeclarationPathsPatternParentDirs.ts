// A `paths` pattern relative to its config with leading `..`s (`"@x/plugins": ["../../plugins/src"]`) names a
// workspace package in declaration emit: module_specifiers.c's normalize() dropped the `..`s, so no such pattern
// matched, and the only name left was the relative path through the package's node_modules link: TS2883 (soldy, a
// npm-linked workspace whose packages map each other by `paths`)
// @filename: /packages/ui/webc/tsconfig.json
{ "compilerOptions": { "strict": true, "declaration": true, "module": "esnext", "moduleResolution": "bundler", "types": [],
  "paths": { "@x/plugins": ["../../plugins/src"], "@x/setup": ["../../setup/index.ts"] } }, "include": ["src/**/*"] }
// @filename: /packages/ui/webc/package.json
{ "name": "@x/webc", "version": "1.0.0", "dependencies": { "@x/plugins": "1.0.0", "@x/setup": "1.0.0" } }
// @filename: /packages/ui/webc/src/a.ts
import { D } from "@x/setup";
export const use = D;
// @filename: /packages/plugins/package.json
{ "name": "@x/plugins", "version": "1.0.0", "type": "module", "exports": { ".": { "types": "./dist/index.d.ts", "default": "./dist/index.js" } } }
// @filename: /packages/plugins/src/index.ts
export * from "./ready";
// @filename: /packages/plugins/src/ready.ts
export class TReadyPlugin { x = 1; }
// @filename: /packages/setup/package.json
{ "name": "@x/setup", "version": "1.0.0", "dependencies": { "@x/plugins": "1.0.0" } }
// @filename: /packages/setup/define.ts
import { TReadyPlugin } from "@x/plugins";
export function make(): { get: () => { plugin: TReadyPlugin } } { return null!; }
// @filename: /packages/setup/index.ts
import { make } from "./define";
export const D = make();
// @link: /packages/plugins -> /node_modules/@x/plugins
// @link: /packages/setup -> /node_modules/@x/setup
