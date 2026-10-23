// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface ElementChildrenAttribute { children: {} } interface IntrinsicElements {} }
declare function One(p: { children: number }): JSX.Element;
const a = <One>{1}​
</One>;
const sentinel: number = "";
