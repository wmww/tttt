// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace JSX { interface IntrinsicElements { [name: string]: any } interface Element {} }
declare const isLogin: boolean;
declare const DEFAULT_NULL_VALUE: string;
declare const HoverCardText: (p: { label: any }) => null;
// `(DEFAULT_NULL_VALUE): (<HoverCardText label=..` is tried as an arrow function with a return type. The type is a function type without
// a parameter list, so the trial fails (typeHasArrowFunctionBlockingParseError) and its errors are dropped.
export const x = (
  <div>
    {isLogin ? (DEFAULT_NULL_VALUE) : (<HoverCardText
      label={<div>{('', { s1: (1, { precision: '' }), s2: (2, { precision: '' }) })}</div>}></HoverCardText>)}
  </div>
);
const sentinel: number = "";
