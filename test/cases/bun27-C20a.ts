// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface ElementChildrenAttribute { children: {} } interface IntrinsicElements {} }
declare function U(p: { a: 1; children: number } | { a: 2 }): JSX.Element;
const u = <U a={1}>{"s"}</U>;
const sentinel: number = "";
