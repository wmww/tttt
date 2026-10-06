// @strict: true
// useParams.tsx/useSearch.tsx: tsc relates `{ from: any; strict: boolean | undefined }` to a union discriminated by
// `strict` (and `from`, whose `from?: never` is `undefined` under strictNullChecks) combination by combination, the
// optional properties with their undefined on both sides (getNonMissingTypeOfSymbol, isPropertySymbolTypeRelated)
type V = { from?: never; strict: false } | { from: any; strict?: true }
declare const b: boolean | undefined
declare const a: any
const ok1: V = { from: a, strict: b }
type U = ({ from?: never; strict: false } & { x?: number }) | ({ from: any; strict?: true } & { x?: number })
const ok2: U = { from: a, strict: b }
declare const o: { from?: any; strict?: boolean }
const ok3: V = o
declare const n: { from: number; strict: boolean | undefined }
const bad: V = n
