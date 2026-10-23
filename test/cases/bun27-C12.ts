// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react-jsx", "jsxImportSource": "starlib", "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.tsx
function F(p: {}) { return null! as { e: 1 }; }
const b = <F />;
const sentinel: number = "";
export {};
// @filename: node_modules/starlib/jsx-runtime.d.ts
export * as JSX from "./ns";
// @filename: node_modules/starlib/more.d.ts
export type LibraryManagedAttributes<C, P> = P & { extra: number };
// @filename: node_modules/starlib/ns.d.ts
export * from "./more";
export interface Element { e: 1 }
export interface IntrinsicElements {}
// @filename: node_modules/starlib/package.json
{ "name": "starlib", "version": "1.0.0" }
