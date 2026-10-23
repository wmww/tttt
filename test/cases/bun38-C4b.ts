// @filename: tsconfig.json
{ "extends": "./base.json", "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsxFactory": "" } }
// @filename: a.tsx
declare global { namespace JSX { interface IntrinsicElements { div: {} } } }
declare const h: any;
export const el = <div />;
export const sentinel: number = "";
// @filename: base.json
{ "compilerOptions": { "jsx": "react", "jsxFactory": "h" } }
