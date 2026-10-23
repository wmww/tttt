// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements {} }
declare function C<const T>(p: { v: T; f: (x: T) => void }): JSX.Element;
const m = <C v={1} f={x => { const one: 1 = x; }} />;
const sentinel: number = "";
