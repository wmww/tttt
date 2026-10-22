// @strict: true
// `export type { X } from "m"` inside `declare global` declares X globally, TS2666 aside (unplugin-auto-import's
// auto-imports.d.ts, bekuto3d's MaybeRefOrGetter). The specifier resolves through the file's `import("./lib")` type
// (tsc doesn't collect the module references in an augmentation's body)
// @filename: lib.ts
export type MaybeRef<T> = T | { value: T };
export interface Box { size: number }
export declare const version: string;
export namespace NS { export type Inner = number }
// @filename: globals.d.ts
export {}
declare global {
  const version: typeof import('./lib').version
  export type { MaybeRef } from './lib'
  export type { Box as GlobalBox } from './lib'
  export * as LibNS from './lib'
}
// @filename: main.ts
export const x: MaybeRef<number> = 1;
export const b: GlobalBox = { size: 1 };
export const bad: GlobalBox = { size: "no" };
export const n: LibNS.NS.Inner = "no";
export const v: string = version;
