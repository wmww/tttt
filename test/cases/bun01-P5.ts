// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react", "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.tsx
export {};
declare global { namespace JSX { interface IntrinsicElements { div: any } interface Element {} } }
class K { React: any; m() { return <React />; } }
const sentinel: number = "";
