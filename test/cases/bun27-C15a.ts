// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } type ElementAttributesProperty = { props: {} } | { props: {}; x: 1 }; interface IntrinsicElements {} }
class K { constructor(p: { c: number }) {} props!: { k: number }; }
const a = <K k={1} />;
const sentinel: number = "";
