// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element {} interface IntrinsicElements { div: {}; call: { (): void; x?: number } } }
declare function use(...x: any[]): void;
declare const ie: JSX.IntrinsicElements;
use(ie.call && <div><call /></div>);
const sentinel: number = "";
