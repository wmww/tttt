// @jsx: preserve
// @noEmit: true
// @filename: a.tsx
// `<<` and `<=` are tokens of their own to tsc's scanner: after a JSX tag name no type arguments, and after an element
// no adjacent element (TS2657); a binary operator aborts the attribute list (isInSomeParsingContext), the element ends
// there and the operator applies to it (bun 43-C13f)
var v = <a << />;
var w = <a <= />;
var x = <a <</a>;
var y = <a<string> />;
var t = (<a><<b/></a>);
