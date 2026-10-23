// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve", "jsxFactory": "h", "jsxFragmentFactory": "Frag" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements {} }
const f = <></>;
const Frag = (p: {}) => null! as JSX.Element;
const sentinel: number = "";
