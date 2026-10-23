// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type P<T> = [Q<T>]; type Q<T> = [T];
type P2<T> = [Q2<T>]; type Q2<T> = [T];
declare function fp<T>(m: P<T>): T;
declare let p2: P2<number>;
fp(p2).toFixed();
const sentinel: number = "";
