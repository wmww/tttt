// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class K {}
declare function f1({ a: b }: {}, c = b.x): void;
declare function f2({ a: b }: {}, c = b()): void;
declare function f3({ a: b }: {}, c = (b = 1)): void;
declare function f4({ a: b }: {}, c = () => b): void;
type F5 = ({ a: b }: {}, c = b.x) => void;
const sentinel: number = "";
