// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve", "noUnusedLocals": true } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements { div: {} } }
function f() { enum React { A } return <div />; }
const sentinel: number = "";
