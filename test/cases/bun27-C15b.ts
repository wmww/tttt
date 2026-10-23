// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } type ElementAttributesProperty = string; interface IntrinsicElements {} }
class K { constructor(p: { c: number }) {} }
const a = <K c={1} />;
const sentinel: number = "";
