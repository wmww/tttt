// @strict: true
// @jsx: preserve
// @jsxFactory: h
// @jsxFragmentFactory: Frag
// The fragment factory declared after the file's first fragment, under jsx: preserve with jsxFragmentFactory (bun case
// 27-P7): tsc's getJSXFragmentType resolves it once per file as a use
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements {} }
const f = <></>;
const g = <></>;
const Frag = (p: {}) => null! as JSX.Element;
