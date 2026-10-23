// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "react" } }
// @filename: a.tsx
namespace N {
  namespace React {
    export declare function createElement(...a: any[]): any;
    export namespace JSX {
      export interface Element {}
      export interface IntrinsicAttributes { key?: string }
      export interface IntrinsicClassAttributes<T> { ref?: T }
      export interface IntrinsicElements { div: {} }
    }
  }
  function C(props: { x: string }) { return null as any; }
  export const e1 = <C x={1} />;
  export const e2 = <C />;
  export const e3 = <C x="a" y={1} />;
  class K { props!: { x: string }; constructor(p: { x: string }) {} render() { return null as any; } }
  export const e4 = <K />;
  export const e5 = <K x="a" y={1} />;
}
const sentinel: number = "";
