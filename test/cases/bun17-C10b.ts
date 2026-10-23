// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react" } }
// @filename: a.tsx
declare namespace JSX { interface Element {} interface IntrinsicElements {} interface ElementChildrenAttribute { children: {} } }
declare const React: any;
function Outer<T>() {
  function E(props: T extends string ? { children: number[] } : { children: number[] }) { return null as any as JSX.Element; }
  <E>{"s"}{"t"}</E>;
}
const sentinel: number = "";
