// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface ElementChildrenAttribute { children: {} } interface IntrinsicElements {} }
declare function L(p: { children: ("x" | "y")[] }): JSX.Element;
const t = <L>{true}{"x"}</L>;
const sentinel: number = "";
