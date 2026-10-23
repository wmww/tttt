// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface ElementChildrenAttribute { children: {} } interface IntrinsicElements {} }
function W<P extends { children: number[] }>(C: (p: P) => JSX.Element) { return <C>{"a"}{"b"}</C>; }
const sentinel: number = "";
