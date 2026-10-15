// A second install of a package version is no file of tsgo's program (its filesByPath maps it to the first copy), so
// its modules are no containers to name a symbol through: tttt named trpc's NextApiHandler `import("next")` through
// the importing package's own (pnpm) copy of next where tsgo has `import("@trpc/server/adapters/next")`
// @declaration: true
// @module: esnext
// @moduleResolution: bundler
// @filename: /node_modules/.pnpm/next@1.0.0_a/node_modules/next/package.json
{ "name": "next", "version": "1.0.0", "types": "index.d.ts" }
// @filename: /node_modules/.pnpm/next@1.0.0_a/node_modules/next/index.d.ts
export interface Handler { x: number }
// @filename: /node_modules/.pnpm/next@1.0.0_b/node_modules/next/package.json
{ "name": "next", "version": "1.0.0", "types": "index.d.ts" }
// @filename: /node_modules/.pnpm/next@1.0.0_b/node_modules/next/index.d.ts
export interface Handler { x: number }
// @filename: /node_modules/.pnpm/lib2@1.0.0/node_modules/lib2/package.json
{ "name": "lib2", "version": "1.0.0" }
// @filename: /node_modules/.pnpm/lib2@1.0.0/node_modules/lib2/sub.d.ts
import type { Handler } from "next";
export type { Handler } from "next";
export declare function handler(): Handler;
// @filename: /a/src/main.ts
import type { Handler } from "next";
export const h: Handler | undefined = undefined;
// @filename: /b/src/y.ts
import { handler } from "lib2/sub";
export default handler();
// @link: /node_modules/.pnpm/next@1.0.0_a/node_modules/next -> /a/node_modules/next
// @link: /node_modules/.pnpm/next@1.0.0_b/node_modules/next -> /b/node_modules/next
// @link: /node_modules/.pnpm/next@1.0.0_b/node_modules/next -> /node_modules/.pnpm/lib2@1.0.0/node_modules/next
// @link: /node_modules/.pnpm/lib2@1.0.0/node_modules/lib2 -> /b/node_modules/lib2
