// @filename: tsconfig.json
{ "compilerOptions": { "noEmit": true, "lib": ["es2022"], "types": [], "jsx": "react", "jsxFactory": "" }, "extends": "./A.json", "files": ["a.tsx"] }
// @filename: A.json
{ "compilerOptions": { "jsxFactory": "h" } }
// @filename: a.tsx
declare namespace JSX { interface IntrinsicElements { div: {} } }
declare function h(...a: any[]): any;
const e = <div/>;
const sentinel: number = "";
