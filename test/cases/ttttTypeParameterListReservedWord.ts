// A type parameter list's element is `in`, `const` or an identifier (tsc's isListElement(TypeParameters)): another
// reserved word ends the list when a statement takes it, else it is TS1139 and skipped (mutation testing,
// declarationEmitArrowFunctionNoRenaming)
// In a JSX file `</` is a token of its own: no type parameter list (bun 56-L06)
// @jsx: preserve
// @filename: a.ts
export type BoundedInteger<
  LowerBound extends number,
  UpperBound extends number

export const toBoundedInteger = <L extends number>(bounds: L) => (n: number): BoundedInteger<L, L> => ({} as any);
type X<export T> = T;
interface I<T, default U> { a: T; b: U }
type Y<
// @filename: b.tsx
class A</T> {}
