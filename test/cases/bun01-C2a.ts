// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react", "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.tsx
import type * as React from "./react";
declare global { namespace JSX { interface IntrinsicElements { div: any } interface Element {} } }
const x1 = <div />;
const sentinel: number = "";
// @filename: react.d.ts
export function createElement(...a: any[]): any;
