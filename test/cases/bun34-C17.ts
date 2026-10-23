// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface IntrinsicElements { div: { a?: { c?: 1 } } } }
<div {...{ a: { b: 1 } }} />;
const sentinel: number = "";
