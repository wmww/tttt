// @strict: true
// @jsx: preserve
// @noEmit: true

// An intrinsic tag is looked up in JSX.IntrinsicElements with getPropertyOfType, which has Object's members: `toString`
// is Object's method, not the index signature's (bun's 27-P6)
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements { [k: string]: { s?: string } } }
const a = <toString s="x" />;
const b = <div s="x" />;
