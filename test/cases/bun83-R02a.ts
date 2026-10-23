// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element {} interface ElementChildrenAttribute { children: {} } interface IntrinsicElements { div: { children?: unknown } } }
declare function A<T>(props: { children?: unknown }): JSX.Element;
type G<T> = T;
var v1 = <A<number>>=1</A>;
var v2 = <A<number>>= </A>;
var v3 = <A<G<number>>>=1</A>;
var v4 = <A<number>>==1</A>;
var v5 = <div><A<number>>=1</A></div>;
var v6 = <A<number> >=1</A>;
const sentinel: number = "";
