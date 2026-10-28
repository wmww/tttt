// @strict: true
// @noEmit: true
// An object binding element whose target after `:` is missing is a missing identifier at its full start, which the
// binder declares: TS2842 and TS7031 for `(Missing)` (mutation testing seed 20261031,
// contextualTypesNegatedTypeLikeConstraintInGenericMappedType2)
declare function matcher(x: any): void;
matcher({
  A: (_) => _.a,
  B: (_) => "fail",

matcher({
  A: (_) => _.a,
});
