// An `import { X }` of a property of what `export =` names, used as a type: missing on an `any` value (vite's CJS
// types, feathers/docs) it is the error type, not lib.dom's global `Plugin`; a value-only property goes on to the
// global type of that name
// @strict: true
// @noUnusedLocals: true
// @module: commonjs
// @esModuleInterop: true
// @lib: esnext,dom
// @filename: anyMod.d.ts
declare const module: any
export = module
// @filename: objMod.d.ts
declare const m: { Plugin: number; Other: number }
export = m
// @filename: main.ts
import type { Plugin } from './anyMod'
export function f(): Plugin {
  return { name: 'x', enforce: 'pre' }
}
// @filename: main2.ts
import { Plugin, Other } from './objMod'
export const a: Plugin = 1 as any
export const b: Other = 1 as any
