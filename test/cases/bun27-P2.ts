// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve", "exactOptionalPropertyTypes": true } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface ElementChildrenAttribute { children: {} } interface IntrinsicElements {} }
declare function T(p: { children: [number, string?] }): JSX.Element;
declare const nu: number | undefined;
const a = <T>{1}{nu}</T>;
const sentinel: number = "";
