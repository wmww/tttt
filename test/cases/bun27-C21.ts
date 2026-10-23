// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements {} }
declare function F(p: { "data-o"?: { alpha: number } }): JSX.Element;
const a = <F data-o={{ alpha: 1, b: 2 }} />;
const sentinel: number = "";
