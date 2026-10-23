// @jsx: preserve
// @strict: true
// comp (refresh 7, @trycompai/design-system's heading.tsx:48): a JSX tag whose type is a generic template literal
// `h${T}` (T constrained to a union of literals). tsc's getUninstantiatedJsxSignaturesOfType takes the apparent type,
// the base constraint "h1" | "h2" | "h3", each member an intrinsic element: no error. tttt reports TS2604 (known
// failing, agents/bugs/sourcesweep_checker.md "Refresh 7"); a type parameter T extends "h1" | "h2" works.
declare namespace JSX {
  interface Element {}
  interface IntrinsicElements {
    h1: { id?: string }; h2: { id?: string }; h3: { id?: string };
  }
}
type L = '1' | '2' | '3';
function A<T extends L = '1'>(level: T, as?: `h${L}`) {
  const C = as ?? (`h${level}` as `h${T}`);
  return <C id="x" />;
}
function B<T extends L>(level: T) {
  const C = `h${level}` as `h${T}`;
  return <C id="x" />;
}
function D<T extends 'h1' | 'h2'>(C: T) {
  return <C id="x" />;
}
