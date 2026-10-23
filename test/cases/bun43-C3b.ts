// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element {} }
declare function Foo(p: { x: "a" }): JSX.Element;
<Foo x="é
   b" />;
const sentinel: number = "";
