// origin: npm freelensapp/freelens @ 088d085 packages/core (tsconfig.bench.json), src/main/get-metrics.injectable.ts:76 (object.entries of an intersection)
// flip: no
// @strict: true
// Inferring to { [P in K]?: X } with K a type parameter: X from the union of the source's property types (an
// intersection's are all of its members'), against the template made optional (tsc's getTemplateTypeFromMappedType)
declare function entries<K extends string, V>(obj: Partial<Record<K, V>>): [K, V][];
interface P { start?: number | string; step?: number }
declare const q: P & { query: string };
const r = entries(q);
declare const q2: P;
const r2 = entries(q2);
declare const q3: { start?: number | string; query: string };
const r3 = entries(q3);
for (const [, value] of entries(q)) value.toString();
