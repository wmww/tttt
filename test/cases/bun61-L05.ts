// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react-jsx", "jsxImportSource": "pre", "module": "node16" } }
// @filename: a.tsx
export const a = <div />;
export const b = <div />;
const sentinel: number = "";
// @filename: node_modules/pre/jsx-runtime.d.ts
export namespace JSX { interface IntrinsicElements { div: {} } interface Element {} } export function jsx(): void;
// @filename: node_modules/pre/package.json
{ "name": "pre", "version": "1.0.0", "type": "module", "exports": { "./jsx-runtime": { "types": "./jsx-runtime.d.ts", "default": "./jsx-runtime.js" } } }
// @filename: package.json
{ "name": "app" }
