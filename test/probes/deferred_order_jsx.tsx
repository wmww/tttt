// Deferred checks: a JSX element's attributes and children are checked after the file's statements (tsc's checkJsxElementDeferred)
// @jsx: preserve
declare namespace JSX { interface Element {} interface IntrinsicElements { [k: string]: any } }
declare function Comp(p: { v: string }): JSX.Element;
const e = <div a={Math.random() ? "jb" : "ja"}>{Math.random() ? "cb" : "ca"}</div>;
const e2 = <Comp v={Math.random() ? "kb" : "ka"} />;
const q = Math.random() ? "ja" : "jb";
const q2 = Math.random() ? "ca" : "cb";
const q3 = Math.random() ? "ka" : "kb";
