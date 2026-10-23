// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react", "module": "esnext", "moduleResolution": "bundler", "isolatedModules": true } }
// @filename: a.tsx
import { React } from "./m";
<div />;
const sentinel: number = "";
// @filename: g.d.ts
declare const React: any;
declare namespace JSX { interface IntrinsicElements { div: any } }
// @filename: m.ts
export interface React {}
