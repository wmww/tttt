// origin: npm elysia@1.4.30 https://github.com/elysiajs/elysia tsconfig.bench.json seed 20261110
// flip: no
// @strict: true
// @moduleResolution: bundler
// @module: esnext
// @noEmit: true
// @filename: /package.json
{ "name": "selfpkg", "version": "1.0.0", "types": "./dist/index.d.ts",
  "exports": { ".": { "types": "./dist/index.d.ts", "import": "./dist/index.mjs" } } }
// @filename: /node_modules/selfpkg/package.json
{ "name": "selfpkg", "version": "0.9.0", "types": "./dist/index.d.ts" }
// @filename: /node_modules/selfpkg/dist/index.d.ts
export declare class Elysia { get(path: string, h: string): this }
// @filename: /example/video.ts
import { Elysia } from 'selfpkg'
new Elysia().get('/', 'x')
