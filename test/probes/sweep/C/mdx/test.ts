import { MDXComponents } from "./types";
import * as runtime from "preact/jsx-runtime";
declare module "./types" {
    namespace JSX {
        type Element = any;
        type ElementClass = any;
        type ElementType = any;
        type IntrinsicElements = any;
    }
}
const c: MDXComponents = {
    a(props) { return null; },
    wrapper(props) { return null; },
};
