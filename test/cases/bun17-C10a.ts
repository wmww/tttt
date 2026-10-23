// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react" } }
// @filename: a.tsx
declare namespace JSX { interface Element {} interface IntrinsicElements {} interface ElementChildrenAttribute { children: {} } }
declare const React: any;
function Outer<T>() {
  function D(props: T extends string ? { children: number } : { children: number }) { return null as any as JSX.Element; }
  <D>{"s"}{"t"}</D>;
}
const sentinel: number = "";
