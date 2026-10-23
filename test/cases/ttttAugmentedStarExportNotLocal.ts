// @strict: true
// @module: esnext
// @moduleResolution: bundler
// @noEmit: true
// An augmentation of a name a module only re-exports through `export *` puts the merged clone into that module's
// exports (tsc's mergeSymbolTable), but it isn't one of the module's file's locals: the file's own import of the name
// doesn't conflict with it, even when the clone it imports is another one ("core" augmented after "pup": crawlee's puppeteer/lib/types.d.ts augmented by puppeteer-extra, a spurious TS2440)
// @filename: node_modules/core/package.json
{ "name": "core", "types": "index.d.ts" }
// @filename: node_modules/core/index.d.ts
export interface PuppeteerNode { launch(): void }
// @filename: node_modules/pup/package.json
{ "name": "pup", "types": "types.d.ts" }
// @filename: node_modules/pup/types.d.ts
import type { PuppeteerNode } from "core";
declare const puppeteer: PuppeteerNode;
export default puppeteer;
export * from "core";
// @filename: extra.d.ts
interface Vanilla { extra(): void }
declare module "pup" {
  interface PuppeteerNode extends Vanilla {}
}
declare module "core" {
  interface PuppeteerNode extends Vanilla {}
}
export {}
// @filename: use.ts
import p, { type PuppeteerNode } from "pup";
p.launch();
declare const n: PuppeteerNode;
n.extra();
