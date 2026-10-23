// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface ElementChildrenAttribute { children: {} } interface IntrinsicElements { div: {} } }
declare function Str(p: { children: string }): JSX.Element;
<Str>​
</Str>;
const sentinel: number = "";
