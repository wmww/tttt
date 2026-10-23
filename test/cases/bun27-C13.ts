// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } type ElementType = Undefined; interface IntrinsicElements {} }
function F(): number { return 1; }
const a = <F />;
const sentinel: number = "";
