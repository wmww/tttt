// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface Element { e: 1 } interface IntrinsicElements { } }
declare const cond: boolean;
const o1 = { q: 1 }, o2 = { r: 2 };
declare function Gen<T>(props: T & { pick?: keyof T }): JSX.Element;
const x1 = <Gen {...(cond ? o2 : o1)} pick="q" />;
declare function Req<T>(props: T & { z: 1 }): JSX.Element;
const x2 = <Req {...(cond ? o2 : o1)} k="" />;
const sentinel: number = "";
