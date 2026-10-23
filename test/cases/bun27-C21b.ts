// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements {} }
declare function F(p: { style?: { className?: string } }): JSX.Element;
const a = <F {...{ style: { class: "x" } }} />;
const sentinel: number = "";
