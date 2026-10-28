// @jsx: preserve
// A JSX attribute's quote right after the `=` (whitespace aside) starts a JSX string, without escapes; after a comment
// it is an ordinary string literal (tsgo's ScanJsxAttributeValue skips whitespace only, then Scan()): `\"` is a quote
// and an octal escape is TS1487 (bun 43-C5a, 43-C5c)
declare namespace JSX { interface Element {} interface IntrinsicElements { a: { b?: string } } }
declare function Foo(p: { x: string }): JSX.Element;
<Foo x=/**/"\"" />;
<Foo x= "\" />;
var v = <a b=/**/"\1" />;
