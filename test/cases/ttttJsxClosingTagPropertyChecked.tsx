// A JSX closing tag's property-access name is checked like the opening one (tsc's checkJsxElementDeferred):
// `</NS.B>` is a TS2339 of its own; an identifier's TS2304 was already reported twice
// @jsx: preserve
// @strict: true
declare namespace JSX { interface Element {} interface IntrinsicElements { div: {} } }
declare const NS: { A: (p: {}) => JSX.Element };
const a = <Foo></Foo>;
const b = <NS.B></NS.B>;
const c = <NS.A></NS.A>;
const d = <div><NS.B /></div>;
