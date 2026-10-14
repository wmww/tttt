// A closing tag is checked by its own name (an intrinsic one looked up in JSX.IntrinsicElements), `</` is one token in a JSX file
// @jsx: preserve
declare namespace JSX {
    interface IntrinsicElements { div: {}; span: {} }
}
declare const a: { b: any };
const e1 = <a.b></div>;
const e2 = <div></nope>;
const e3 = <span></a.b>;
const e4 = x </y;
