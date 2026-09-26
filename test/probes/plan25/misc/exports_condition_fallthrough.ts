// @moduleResolution: bundler
// @module: esnext
// @strict: true
// @noImplicitAny: true
// @filename: /node_modules/cva/package.json
{
  "name": "cva",
  "version": "1.0.0",
  "exports": {
    ".": {
      "import": "./dist/index.mjs",
      "require": "./dist/index.js",
      "types": "./dist/index.d.ts"
    }
  }
}
// @filename: /node_modules/cva/dist/index.mjs
export const cva = 1;
// @filename: /node_modules/cva/dist/index.js
exports.cva = 1;
// @filename: /node_modules/cva/dist/index.d.ts
export declare const cva: number;
// @filename: /main.ts
import { cva } from "cva";
const n: number = cva;
