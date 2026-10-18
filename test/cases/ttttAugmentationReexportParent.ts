// An augmentation that merges into a re-exported namespace (`export type { JSX }` of another file's) makes the module
// it augments that namespace's parent (tsc's mergeSymbolTable sets the merged clone's parent): named through
// types/index, not types/jsx
// @module: esnext
// @moduleResolution: bundler
// @filename: /node_modules/solid/package.json
{ "name": "solid", "types": "./types/index.d.ts" }
// @filename: /node_modules/solid/types/jsx.d.ts
export namespace JSX {
    type Element = Node | number | null;
    interface Node { solid: true }
}
// @filename: /node_modules/solid/types/index.d.ts
import type { JSX } from "./jsx.js";
export type { JSX };
// @filename: /node_modules/web/index.d.ts
import type { JSX } from "solid";
export declare function mount(fn: () => JSX.Element): void;
// @filename: /node_modules/@types/react/index.d.ts
export = React;
export as namespace React;
declare namespace React {
    namespace JSX { interface Element { react: true } }
}
// @filename: /a.ts
import { mount } from "web";
declare const e: React.JSX.Element;
mount(() => e);
// @filename: /node_modules/router/index.d.ts
declare module "solid" {
    namespace JSX {
        interface Extra { x: 1 }
    }
}
export {};
// @filename: /b.ts
import "router";
