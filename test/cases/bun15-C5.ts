// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true, "jsx": "preserve" } }
// @filename: a.tsx
enum React { A }
export const a = <div />;
export const sentinel: number = "";
// @filename: jsx.d.ts
declare namespace JSX { interface Element {} interface IntrinsicElements { div: {}; } }
