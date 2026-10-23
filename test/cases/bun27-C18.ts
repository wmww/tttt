// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements {} }
declare function W(p: { x?: number }): JSX.Element;
const s = <W x={1} x={3} {...{ x: 4 }} />;
const sentinel: number = "";
