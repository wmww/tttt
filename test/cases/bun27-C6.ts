// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements {} }
declare namespace React { function createElement(type: (props: any, ctx?: any) => any, props?: any): JSX.Element; }
function g<T extends (p: {}, c: {}, u: {}) => null>(X: T) { return <X />; }
const sentinel: number = "";
