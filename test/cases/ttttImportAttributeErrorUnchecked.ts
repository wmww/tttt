// @target: esnext
// @module: preserve
// @moduleResolution: bundler
// @noEmit: true

// An import or export whose attribute values aren't all string literals (TS2858) isn't checked further (tsc's
// checkExternalImportOrExportDeclaration returns false): no TS2307 for its unresolved module
// @filename: /types.d.ts
declare module "*.style" with { type: "css" } {
    export interface Style { kind: "css"; }
}
declare module "formatted/*.style" with { type: "css", format: "module" } {
    export interface FormattedStyle { kind: "css-module"; }
}
// @filename: /index.ts
export type { FormattedStyle as A } from "formatted/a.style" with { type: 42, format: "module" };
export type { FormattedStyle as B } from "formatted/b.style" with { type: "x", format: "module" };
export type { Style as C } from "c.style" with { type: "x" };
export type { Style as D } from "d.style" with { type: 42 };
import type { Style as E } from "e.style" with { type: 42 };
