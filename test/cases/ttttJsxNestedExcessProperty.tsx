// @strict: true
// @jsx: preserve
// @noEmit: true

// An excess property nested in a JSX attribute's value is reported like tsc's hasExcessProperties with the JSX
// attributes as the error node: "Property 'b' does not exist on type", in the chain at the element, not TS2353 at the
// property (bun's 27-C21, 27-C21b, 34-C17)
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements {} }
declare function F(p: { "data-o"?: { alpha: number } }): JSX.Element;
const a = <F data-o={{ alpha: 1, b: 2 }} />;
declare function G(p: { style?: { className?: string } }): JSX.Element;
const b = <G style={{ class: "x" }} />;
