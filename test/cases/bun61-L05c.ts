// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react-jsx", "jsxImportSource": "./x", "module": "nodenext" } }
// @filename: a.tsx
export const a = <div />;
export const b = <div />;
const sentinel: number = "";
// @filename: package.json
{ "type": "module" }
// @filename: x/jsx-runtime.ts
export namespace JSX { export interface IntrinsicElements { div: {} } export interface Element {} } export function jsx(): void {}
