// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react" } }
// @filename: a.tsx
declare namespace JSX { interface Element { g: 1 } interface IntrinsicElements { div: { g: string } } }
namespace Outer {
  namespace React { export declare function createElement(...a: any[]): any; export namespace JSX { export interface Element { l: 1 } export interface IntrinsicElements { div: { l: number } } } }
  export const a = <div l={1} />;
}
const sentinel: number = "";
