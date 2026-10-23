// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace Other { type ET = "div"; }
declare namespace JSX { interface Element { e: 1 } export import ElementType = Other.ET; interface IntrinsicElements { div: {}; span: {} } }
const a = <span />;
const sentinel: number = "";
