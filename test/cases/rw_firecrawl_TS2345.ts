// origin: npm firecrawl (named) https://github.com/firecrawl/firecrawl apps/api/tsconfig.bench.json seed 20269002 (commit e1dc7a43)
// flip: no
// Cut by hand from apps/api/src/controllers/v2/types.ts (extractTransformImpl): `liftCheckPromptInjection(applyScrapeOptionsDefaults(obj))`
// with obj a T extends Base | undefined: tsc infers the intersection T & { skip }, tttt fell back to the constraint
// @strict: true
type Base = { formats: ({ type: "json" } | "markdown")[]; timeout: number; skip?: boolean };
declare const applyDefaults: <T extends Base>(obj: T) => T & { skip: boolean };
declare function lift<T extends { formats?: unknown[]; check?: unknown }>(options: T): T;
const impl = <T extends Base | undefined>(obj: T): T extends undefined ? undefined : T => {
  if (!obj) return obj as T extends undefined ? undefined : T;
  let result = lift(applyDefaults(obj));
  result = { ...result, timeout: 60000 };
  return result as T extends undefined ? undefined : T;
};
