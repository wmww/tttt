// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements {} }
declare function D<T>(p: { v: T; f: (x: T) => void }): JSX.Element;
declare const s: unique symbol;
const m = <D v={s as typeof s} f={x => { const t: never = x; }} />;
const sentinel: number = "";
