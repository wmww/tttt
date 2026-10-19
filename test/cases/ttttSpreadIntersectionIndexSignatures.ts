// @strict: true
// Spreading an intersection takes its index signatures as the intersection has them: one per key type, of the
// intersection of the members' value types, printed `x` (tsgo's resolveIntersectionTypeMembers appendIndexInfo).
// supabase studio's Logs.utils.otel.ts had two `[x: string]` and a spurious TS2322
interface Metadata { a: number }
interface CustomLogData {
  query?: string | undefined
  [other: string]: unknown
}
interface PreviewLogData extends CustomLogData {
  id: string
  timestamp: number
  metadata?: Metadata
  [key: string]: string | number | Metadata | undefined
}
type LogData = CustomLogData & PreviewLogData
declare const base: LogData;
declare const attrs: Record<string, string>;
export const r: LogData = { ...base, ...attrs };
export const s = { ...base, ...attrs };
export const t = { ...base };
