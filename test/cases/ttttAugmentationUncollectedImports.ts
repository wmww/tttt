// @strict: true
// @module: esnext
// @moduleResolution: bundler
// The import and export declarations in a module's augmentations (`declare global {}`, `declare module "m" {}`) are
// not among the file's imports (tsc's collectModuleReferences doesn't walk them): their module is found only through
// an import of the same name and mode the file does have (here a `typeof import("pkg")`), else TS2307 and any
// @filename: /node_modules/pkg/package.json
{ "name": "pkg", "types": "index.d.ts" }
// @filename: /node_modules/pkg/index.d.ts
export type Ref<T> = { value: T };
export declare const v: number;
// @filename: /node_modules/other/package.json
{ "name": "other", "types": "index.d.ts" }
// @filename: /node_modules/other/index.d.ts
export interface Other { other: true }
// @filename: /lib.ts
export interface Box { size: number }
// @filename: /alone.d.ts
export {}
declare global {
  export type { Ref as AloneRef } from 'pkg'
  export type { Box as AloneBox } from './lib'
}
// @filename: /twin.d.ts
export {}
declare global {
  const v2: typeof import('pkg').v
}
declare global {
  export type { Ref as TwinRef } from 'pkg'
}
// @filename: /augment.ts
export interface Base { base: true }
declare module './augment' {
  export { Other } from 'other'
  interface Base { extra: number }
}
// @filename: /main.ts
import type { Other } from './augment';
export const a: AloneRef<number> = { value: "any" };
export const b: AloneBox = { size: "any" };
export const t: TwinRef<number> = { value: "no" };
export const o: Other = { other: false };
