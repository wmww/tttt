// @strict: true
// An `export type { X } from "m"` inside `declare global` declares X globally (unplugin-auto-import's generated
// auto-imports.d.ts does it for Vue's types: bekuto3d's MaybeRefOrGetter). tttt reports TS2304 at the uses
// @filename: lib.ts
export type MaybeRef<T> = T | { value: T };
export interface Box { size: number }
// @filename: globals.d.ts
export {}
declare global {
  export type { MaybeRef } from './lib'
  export type { Box as GlobalBox } from './lib'
}
// @filename: main.ts
export const x: MaybeRef<number> = 1;
export const b: GlobalBox = { size: 1 };
export const bad: GlobalBox = { size: "no" };
