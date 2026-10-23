// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "jsx": "react" } }
// @filename: a.tsx
export const a = <div />;
const React: any = 0;
const sentinel: number = "";
// @filename: jsx.d.ts
declare namespace JSX { interface Element {} interface IntrinsicElements { div: {}; } }
