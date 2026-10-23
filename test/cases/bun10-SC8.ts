// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noImplicitAny": false, "noUnusedLocals": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Foo {}
export type F1 = (Foo) => void;
export const sentinel: number = "";
