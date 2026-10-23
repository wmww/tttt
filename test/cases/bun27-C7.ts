// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react", "jsxFactory": "h", "jsxFragmentFactory": "Fragment" } }
// @filename: a.tsx
declare function h(...a: any[]): any;
declare namespace h { namespace JSX { interface Element { e: 1 } interface IntrinsicElements {} } }
declare function Fragment(p: { x: number }): h.JSX.Element;
const f = <></>;
const sentinel: number = "";
