// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements {} }
declare namespace React { function createElement(...a: any[]): any; const Fragment: (p: { x: number }) => JSX.Element; }
function f(React: any) { return <></>; }
const a = <></>;
const sentinel: number = "";
