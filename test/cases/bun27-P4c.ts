// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } type IntrinsicClassAttributes = Undefined; interface IntrinsicElements {} }
class K { constructor(p: { c: number }) {} }
const a = <K />;
const sentinel: number = "";
