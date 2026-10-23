// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element {} interface IntrinsicElements { div: {}; a: {} } }
declare function use(...x: any[]): void;
function div() {}
if (div) { use(<div />); }
if (div) { use(<div></div>); }
use(div && <a><div /></a>);
async function t(a: Promise<string>) { if (a) { use(<a />); } }
const sentinel: number = "";
