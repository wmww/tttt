// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "jsx": "react" } }
// @filename: a.tsx
import type React from "./r";
export const a = <div />;
const sentinel: number = "";
// @filename: jsx.d.ts
declare namespace JSX { interface Element {} interface IntrinsicElements { div: {}; } }
// @filename: r.ts
const React = { createElement(...a: any[]): any {} }; export default React;
