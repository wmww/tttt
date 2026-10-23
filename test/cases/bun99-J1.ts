// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2023", "dom"], "jsx": "react-jsx", "module": "esnext", "moduleResolution": "bundler", "allowJs": true, "checkJs": true}, "files": ["App.TSX", "lower.tsx", "z.JSX", "Mixed.Tsx", "s.ts", "imports.ts"]}
// @filename: App.TSX
export const a = <div />;
// @filename: Mixed.Tsx
export const d = <div />;
// @filename: imports.ts
import { a } from "./App.TSX";
import { b } from "./lower.tsx";
import { b as b2 } from "./lower";
import { c } from "./z.JSX";
import { d } from "./Mixed";
export const all = [a, b, b2, c, d];
// @filename: lower.tsx
export const b = <div />;
// @filename: s.ts
const sentinel: number = ""; export {};
// @filename: z.JSX
export const c = <div />;
