// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowJs": true, "checkJs": true, "jsx": "preserve" }, "files": ["x.TSX", "z.JSX", "a.ts"] }
// @filename: a.ts
const sentinel: number = "";
// @filename: x.TSX
declare namespace JSX { interface IntrinsicElements { div: { id?: string } } }
const el = <div id={1} />;
// @filename: z.JSX
const el2 = <div id={2} />;
