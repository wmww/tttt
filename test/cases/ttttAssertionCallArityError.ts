// @strict: true
// An assertion call with an arity error resolves to its candidate all the same (tsc's getCandidateForOverloadFailure):
// TS2775 besides TS2554 when the callee has no explicit type (missing_checks.md, assertionTypePredicates2 add-arg).
// Not modelled: several overloads, whose failure candidate is tsc's union signature with the first one's predicate
declare function isString(x: unknown): asserts x is string;
const f = (x: unknown): asserts x is string => {};
const obj = { g: f };
function t(v: unknown) {
  isString(v, 1);
  obj.g(v, 2);
  obj.g(v);
}
