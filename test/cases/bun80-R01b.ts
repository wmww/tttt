// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface IntrinsicElements { div: any } interface Element {} }
const j1 = <div #constructor="x" />;
const j2 = <div a="1" #constructor />;
const sentinel: number = "";
