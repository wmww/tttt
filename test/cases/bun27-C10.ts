// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } type LibraryManagedAttributes<C, P> = { c: C }; interface IntrinsicElements { div: {} } }
declare const Lit: "div";
const a = <Lit c={1} />;
const sentinel: number = "";
