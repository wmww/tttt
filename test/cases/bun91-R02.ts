// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react" } }
// @filename: a.tsx
declare namespace JSX { interface Element {} interface IntrinsicElements { div: any } }
declare const React: any;
const enum E { A = 1 }
let j1 = <div a={E} />;
let j2 = <div>{E}</div>;
let j3 = <div a={(E)} />;
let j4 = <div {...E} />;
let j5 = <div a={E.A}>{E.A}</div>;
const sentinel: number = "";
