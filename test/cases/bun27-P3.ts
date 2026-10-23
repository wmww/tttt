// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements {} }
declare function G<T>(p: { a: (n: number) => T; f: (x: T) => void }): JSX.Element;
declare const anyV: any;
const g = <G {...anyV} a={n => "s"} f={x => x.length} />;
const sentinel: number = "";
