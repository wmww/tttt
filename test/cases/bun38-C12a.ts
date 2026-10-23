// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve", "jsxImportSource": "" } }
// @filename: a.tsx
declare global { namespace JSX { interface IntrinsicElements { div: {} } } }
export const el = <div />;
export const sentinel: number = "";
