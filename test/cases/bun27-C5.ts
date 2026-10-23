// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface IntrinsicAttributes { cb?: (x: number) => void } interface IntrinsicElements {} }
declare const A: any;
const a = <A cb={x => x.toFixed()} />;
const sentinel: number = "";
