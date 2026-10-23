// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react", "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.tsx
export {};
declare global { namespace JSX { interface IntrinsicElements { div: any } } }
<div />;
const React: any = 0;
const sentinel: number = "";
