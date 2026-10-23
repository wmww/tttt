// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react" } }
// @filename: a.tsx
import type * as React from "./lib";
export const a = <div />;
const sentinel: number = "";
// @filename: lib.ts
export declare function createElement(...a: any[]): any;
export declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements { div: {} } }
