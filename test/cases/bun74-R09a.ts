// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare global { namespace JSX { interface IntrinsicElements { [k: string]: any } } }
export function List(props: { items: { name: string }[] }) {
  return <ul>{props.items.map(package => <li>{package.name}</li>)}</ul>;
}
export const frag = <>{[1].map(static => static)}</>;
const sentinel: number = "";
