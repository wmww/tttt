// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es5"], "jsx": "preserve" } }
// @filename: a.tsx
interface Iterable<T> {}
declare namespace JSX { interface Element {} interface ElementChildrenAttribute { children: {} } interface IntrinsicElements {} }
declare function C(props: { children: number }): JSX.Element;
declare function D(props: { children: number[] }): JSX.Element;
declare function E(props: { children: number | string[] }): JSX.Element;
const a1 = <C>{"x"}{"y"}</C>;
const a2 = <C>{"x"}</C>;
const a3 = <D>{"x"}{"y"}</D>;
const a4 = <D>{"x"}</D>;
const a5 = <E>{true}{"y"}</E>;
const a6 = <E>{true}</E>;
const sentinel: number = "";
