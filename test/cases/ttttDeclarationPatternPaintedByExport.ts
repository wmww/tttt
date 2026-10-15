// A destructuring variable statement without initializers in its pattern, not exported itself but whose names an
// `export { a, b }` paints, is written whole (tsgo's getBindingNameVisible: any visible name makes the pattern visible),
// discord.js's shiki.bundle `declare const { codeToHtml, ... }: ShorthandsBundle<...>`
// @declaration: true
// @module: esnext
// @moduleResolution: bundler
// @filename: /node_modules/lib/package.json
{ "name": "lib", "version": "1.0.0", "types": "index.d.ts" }
// @filename: /node_modules/lib/index.d.ts
export interface Bundle<T> { a: () => T; b: () => T; c: () => T }
export declare function make<T>(): Bundle<T>;
// @filename: /a.ts
import { make } from "lib";
const { a, b } = make<string>();
const { c } = make<number>();
export { a, b };
