// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface IntrinsicElements { a: any } }
var v = <a #b />;
const sentinel: number = "";
