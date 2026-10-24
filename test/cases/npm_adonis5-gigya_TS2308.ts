// origin: npm adonis5-gigya@2.0.0 https://github.com/renzosunicgadonis5-gigya adonis-typings/index.ts seed 20261107
// flip: no
// @strict: true
// @filename: node_modules/ga/index.d.ts
export type RequestParams = { a: string } & Partial<{ b: string }>
export declare class Gigya {}
// @filename: node_modules/gb/index.d.ts
export type RequestParams<P = {}> = { a: string } & P
export declare class Factory {}
// @filename: index.ts
declare module 'ioc' {
  export * from 'ga'
  export * from 'gb'
}
