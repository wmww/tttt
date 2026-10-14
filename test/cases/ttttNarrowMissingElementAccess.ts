// @strict: true
// @noEmit: true
// An element access of a property that isn't there (TS7053) is the error type, which control flow narrows like any
// other reference (tsc's checkElementAccessExpression: getFlowTypeOfAccessExpression with errorType): renovate's
// `isNonEmptyArray(cfg['pip-compile'].managerFilePatterns)` guards a `.map(p => ...)` whose p is then unknown
declare function isNonEmptyArray<T = unknown, Item = unknown>(value: T | Item[]): value is [Item, ...Item[]];
declare function isNonEmptyObject(value: unknown): value is Record<string, unknown>;
interface Cfg { a?: string; [k: number]: string }
declare const c: Cfg;
if (
  // @ts-expect-error
  isNonEmptyObject(c['pip-compile']) &&
  // @ts-expect-error
  isNonEmptyArray(c['pip-compile'].m)
) {
  // @ts-expect-error
  c['pip-compile'].m = c['pip-compile'].m.map((f) => {
    return f as string;
  });
}
declare const f: {};
// @ts-expect-error
if (isNonEmptyObject(f['p-c'])) {
  // @ts-expect-error
  f['p-c'];
}
