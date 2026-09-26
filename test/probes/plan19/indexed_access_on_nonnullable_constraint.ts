// tsc: no errors. tttt: TS2322 x2 (and in valibot types/issue.ts, 12x TS2344 through InferInput/InferIssue).
// tsc's constraint of `(O['a'] & {})['b']` (NonNullable<O['a']>['b']) indexes the *base constraint* of the object,
// which for an intersection is the intersection of its members' constraints (getBaseConstraintOfType); tttt only handles type-param objects.
function h<O extends { a?: { b: string } }>(x: NonNullable<O['a']>['b'], u: unknown) {
  const s: string = x;                          // source side: constraint of the indexed access
}
function k<O extends { a?: { b: unknown } }>(u: unknown) {
  const t: NonNullable<O['a']>['b'] = u;        // target side: write constraint of the indexed access
}
