// @declaration: true
// The comments around a parameter's binding pattern's elements (intervening ones and JSDoc leading ones)
export function normalizePages({
  list,
  route,
  /** @default '' */
  docsRoot = '',
  /** @default X */
  pageThemeContext = 1
}: {
  list: string[]; route: string; docsRoot?: string; pageThemeContext?: number
}): void {}
export class C {
  m({ a, /* plain */ b, /** doc */ c }: { a: 1; b: 2; c: 3 }, [/** first */ x, y]: [1, 2]) {}
}
