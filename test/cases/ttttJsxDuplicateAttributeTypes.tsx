// Attributes of one name are one symbol: each name is typed by the first one (its value declaration)
// @jsx: react
declare const React: any
declare namespace JSX {
    interface IntrinsicElements {
        [k: string]: any
    }
}
const X: any
const a: any
const e1 = <X a={1} a />;
const e2 = <X b b={"s"} />;
const e3 = <X c={1} c={"s"} />;
