// @module: esnext
// @moduleResolution: bundler
// @filename: /global.d.ts
declare var someGlobal: number;
// @filename: /main.ts
import "./global.d.ts";
export const x = someGlobal;
