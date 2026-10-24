// origin: npm cellajs/cella@main https://github.com/cellajs/cella frontend/tsconfig.json seed 20261402
// flip: yes
// @strict: true
// @noEmit: true
// @lib: es2022
// Object.entries over `Partial<Record<keyof T, string>>` (T generic): inferring `{ [s: string]: U }` from the generic
// mapped type gives tsgo `Record<keyof T, string>[string] | undefined`, tttt `unknown` (then `{}` after the truthy
// test, TS2322 against string; the backend has the same shape as TS2345).
export const rewriteUrlToSlug = <T extends Record<string, string>>(params: T, slugOverrides: Partial<Record<keyof T, string>>) => {
  const newParams: Record<string, string> = { ...params };
  for (const [key, slug] of Object.entries(slugOverrides)) {
    if (slug && params[key] !== slug) {
      newParams[key] = slug;
    }
  }
  return newParams;
};
