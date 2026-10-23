// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react", "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.tsx
export {};
declare global { namespace JSX { interface IntrinsicElements { div: any } interface Element {} } }
function unrelated(React: number) {}
const x1 = <div />;
const sentinel: number = "";
// @filename: react.d.ts
export function createElement(...a: any[]): any; export as namespace React;
