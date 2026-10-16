// @declaration: true
// @declarationMap: true
// An object literal's string literal property name printed otherwise (`'a'` as `a`, `"a-b"` requoted) keeps the
// name's mappings (tsgo's reuseName keeps the range: query's codemod fixtures)
export const f = () => {
  return {
    'useErrorBoundary': true,
    "a-b": 1,
    'c': 'x',
  }
}
