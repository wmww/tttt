// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react-jsx", "jsxImportSource": "@types/react", "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.tsx
export const a = <div />;
export const b = <div />;
const sentinel: number = "";
// @filename: node_modules/@types/react/jsx-runtime.d.ts
export namespace JSX { interface IntrinsicElements { div: {} } interface Element {} } export function jsx(): void;
// @filename: node_modules/@types/react/package.json
{ "name": "@types/react", "version": "1.0.0" }
