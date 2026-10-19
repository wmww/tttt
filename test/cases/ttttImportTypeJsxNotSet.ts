// TS6142 for an import type into a .tsx file without --jsx too (tsc's resolveExternalModule runs for import types): dyad's
// `importOriginal<typeof import("../lib/toast")>()`
// @filename: b.tsx
export const x = 1
// @filename: a.ts
import { x } from "./b"
type T = typeof import("./b")
declare function f<U>(): U
const y = f<typeof import("./b")>()
export { x, y }
export type { T }
