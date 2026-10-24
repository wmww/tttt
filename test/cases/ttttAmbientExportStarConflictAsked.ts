// @strict: true
// @filename: node_modules/ga/index.d.ts
// tsc finds an ambient module's `export *` conflicts (TS2308) only when something asks for its exports, and with one
// checker in program order the declaring file's diagnostics are already collected when a later file asks: 'ioc' is
// asked for by before.ts, ahead of its declaration (TS2308), 'ioc2' only by after.ts, behind its declaration (none)
export type RequestParams = { a: string } & Partial<{ b: string }>
export declare class Gigya {}
// @filename: node_modules/gb/index.d.ts
export type RequestParams<P = {}> = { a: string } & P
export declare class Factory {}
// @filename: before.ts
import { Gigya } from 'ioc';
export const g: Gigya = null!;
// @filename: declare.ts
declare module 'ioc' {
  export * from 'ga'
  export * from 'gb'
}
declare module 'ioc2' {
  export * from 'ga'
  export * from 'gb'
}
// @filename: after.ts
import { Factory } from 'ioc2';
export const f: Factory = null!;
