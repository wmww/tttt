// Flow's type cast `(a.b: any)(...args)`: both parsers recover through an arrow function missing its `=>`, but tttt
// also reports TS1200 (line terminator before an arrow) at the end of the file, which tsgo doesn't. Known failing (agents/bugs/missing_checks.md, Parser recovery). Babel's
// transform-spread call-context/flow-type-cast fixture
declare const a: { b: Function }, args: any[];
(a.b: any)(...args)
