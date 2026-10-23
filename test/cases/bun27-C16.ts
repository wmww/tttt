// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve", "allowJs": true, "checkJs": true } }
// @filename: a.jsx
/** @param {{ a?: string }} p */
function F(p) { return /** @type {any} */ (null); }
const a1 = <F q={x => x.nope} />;
/** @type {number} */
const sentinel = "";
// @filename: g.d.ts
declare namespace JSX { interface Element { e: 1 } type LibraryManagedAttributes<C, P, Q> = P & { q: (x: Q) => void }; interface IntrinsicElements { div: {} } }
