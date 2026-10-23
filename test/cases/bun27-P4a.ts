// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } type IntrinsicAttributes = Undefined; interface IntrinsicElements {} }
declare function F(p: { a: number }): JSX.Element;
const a = <F />;
const sentinel: number = "";
