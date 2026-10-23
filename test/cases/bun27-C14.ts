// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } type IntrinsicElements = { a: { v: 1 } } | { a: { v: 2 } }; }
declare const La: "a";
const u = <La v={3} />;
const sentinel: number = "";
