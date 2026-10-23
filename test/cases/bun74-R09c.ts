// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface IntrinsicElements { [k: string]: any } }
const a = <form onSubmit={function (e: any) { arguments = e; }} />;
const b = <div a={(eval: any) => 1}>{function arguments() {}}</div>;
const sentinel: number = "";
