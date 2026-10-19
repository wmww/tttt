// @module: esnext
// @moduleResolution: bundler
// Two `export *`s re-exporting an import from a missing module export the same thing, tsc's unknownSymbol: no TS2308
// (atproto's lex-client checked before its referenced projects are built). Different resolved things still clash
// @filename: a.ts
import type { X } from 'nope'
export type { X }
export const Y = 1
// @filename: b.ts
import { type X } from 'nope2'
export { type X }
export const Y = 2
// @filename: index.ts
export * from './a'
export * from './b'
