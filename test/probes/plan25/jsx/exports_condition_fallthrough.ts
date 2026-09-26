// @module: esnext
// @moduleResolution: bundler
// @strict: true
// @noImplicitAny: true
// An exports-map condition whose target doesn't resolve (the "import" .mjs has no .d.mts next to it) falls through to
// the next condition ("types"). Reduced from class-variance-authority 0.7.0 (TS7016 in the React corpus; its
// VariantProps becoming `any` makes ButtonProps any, hence the pagination.tsx TS2741/TS2783).
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
  },
  "types": "./dist/index.d.ts"
}
// @filename: /node_modules/cva/dist/index.mjs
export const cva = 1;
// @filename: /node_modules/cva/dist/index.d.ts
export declare const cva: number;
// @filename: /main.ts
import { cva } from "cva";
const n: number = cva;
