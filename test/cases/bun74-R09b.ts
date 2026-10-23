// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
declare global { namespace JSX { interface IntrinsicElements { [k: string]: any } } }
export function Button() {
  return <button onClick={() => { const interface = 1; return interface; }}>ok</button>;
}
export function Input(public: any) {
  return <input value={public} {...public} />;
}
export class View { render() { return <div ref={(static: any) => static}></div>; } }
const sentinel: number = "";
