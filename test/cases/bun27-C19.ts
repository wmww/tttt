// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface ElementChildrenAttribute { children: {} } interface IntrinsicElements {} }
type Cond<T> = T extends string ? { children: number } : { children: string };
function f<T>(C: (p: Cond<T>) => JSX.Element) { return <C>{1}{2}</C>; }
const sentinel: number = "";
