// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface ElementChildrenAttribute { children: {} } interface IntrinsicElements { div: {} } }
declare function One(p: { children: JSX.Element }): JSX.Element;
<One>
​<div/>
</One>;
const sentinel: number = "";
