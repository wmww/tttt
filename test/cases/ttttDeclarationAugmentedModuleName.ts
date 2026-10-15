// A module file that a `declare module "vite" {}` augments is named "vite" by import types (tsgo's
// tryGetModuleNameFromAmbientModule comes before any path to the file), and sorts first among the containers of what
// it exports (getSymbolChain's sortByBestName), even when no path from here reaches it: astro's
// `import("vite").UserConfig` where tttt printed vitest/config's re-export `ViteUserConfig`
// @declaration: true
// @module: commonjs
// @filename: /node_modules/vitest/package.json
{ "name": "vitest", "version": "1.0.0", "types": "index.d.ts" }
// @filename: /node_modules/vitest/node_modules/vite/package.json
{ "name": "vite", "version": "1.0.0", "types": "index.d.ts" }
// @filename: /node_modules/vitest/node_modules/vite/index.d.ts
export interface UserConfig { root?: string }
export interface Other { x: number }
// @filename: /node_modules/vitest/index.d.ts
import { UserConfig } from "vite";
export { UserConfig as ViteUserConfig, Other } from "vite";
declare module "vite" {
    interface UserConfig { test?: { include?: string[] } }
}
export declare function defineConfig(config: UserConfig): UserConfig;
export declare function other(): import("vite").Other;
// @filename: /a.ts
import { defineConfig, other } from "vitest";
export default defineConfig({ test: { include: [] } });
export const o = other();
