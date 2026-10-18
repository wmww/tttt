// A source intersection is normalized like a target one (tsc's getNormalizedUnionOrIntersectionType):
// `Partial<T>[K] & null` is reported as `T[K] & null` (vitepress's deepMerge)
// @strict: true
declare const isObject: any;
function deepMerge<T>(target: T, source: Partial<T>): T {
  const result = { ...target } as any;
  for (const key in source) {
    const value = source[key];
    if (value === undefined) continue;
    if (isObject(value) && isObject(result[key])) {
      result[key] = deepMerge(result[key], value);
    }
  }
  return result;
}
