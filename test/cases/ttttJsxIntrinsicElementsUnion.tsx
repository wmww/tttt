// @strict: true
// @jsx: preserve
// @noEmit: true

// A union JSX.IntrinsicElements has the tag's union property when every member has it (tsc's getPropertyOfType):
// a tag variable of literal type "a" checks its attributes against `{ v: 1 } | { v: 2 }` (bun's 27-C14)
declare namespace JSX { interface Element { e: 1 } type IntrinsicElements = { a: { v: 1 } } | { a: { v: 2 } }; }
declare const La: "a";
const u = <La v={3} />;
const w = <La v={2} />;
