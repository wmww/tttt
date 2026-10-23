// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare namespace Other { type LMA<C, P> = P & { extra: number }; }
declare namespace JSX { interface Element { e: 1 } export import LibraryManagedAttributes = Other.LMA; interface IntrinsicElements {} }
declare function F(p: {}): JSX.Element;
const a = <F />;
const sentinel: number = "";
