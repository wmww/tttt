// A JSX element's type argument list takes a comma as an element too (tsc's isListElement(TypeArguments)): a missing
// type, TS1110 at the comma, then the comma; a `>` after the last comma ends the list
// @jsx: preserve
// @noEmit: true
declare var f: any;
declare namespace JSX { interface IntrinsicElements { [k: string]: any } }
let a = <c<,>/>;
let b = <c<,,>/>;
let d = <c<A,,B>/>;
let e = f<,>();
let g = f<,,>();
