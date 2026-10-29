// @strict: true
// @jsx: react
// @module: esnext
// The JSX factory declared after an element that uses it is used before its declaration (bun cases 01-C2b, 27-P7):
// tsc resolves it at the element's tag with the not-found message of jsx: react, which checks block-scoped uses
export {};
declare global { namespace JSX { interface IntrinsicElements { div: any } interface Element {} } }
const x1 = <div />;
const x2 = <></>;
function later() { return <div />; }
const React = { createElement(...a: any[]): any {}, Fragment: 1 };
const x3 = <div />;
