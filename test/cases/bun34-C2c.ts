// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react", "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.tsx
export {};
declare global { namespace JSX { interface IntrinsicElements { div: any } } }
function g(React: any = <div />) {}
function h(a = <div />, React: any) {}
const sentinel: number = "";
