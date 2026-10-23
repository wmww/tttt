// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
function C(...args: []): JSX.Element { return <div />; }
const b = <C x={1} />;
const sentinel: number = "";
// @filename: jsx.d.ts
declare namespace JSX { interface Element {} interface IntrinsicElements { div: {}; } interface IntrinsicAttributes { key?: string } }
