// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "declaration": true } }
// @filename: a.ts
declare const k: string | undefined;
declare const o: { p?: string } | undefined;
export const n1: number = ({ [k!]: v }: Record<string, number>) => v;
export const n2: number = ({ [k!!]: v }: Record<string, number>) => v;
export const n3: number = ({ [k!!!]: v }: Record<string, number>) => v;
export const n4: number = ({ [(k!!)!]: v }: Record<string, number>) => v;
export const n5: number = ({ [o!!.p!!]: v }: Record<string, number>) => v;
export const n6: number = ({ [k! ! /* c */ !]: v }: Record<string, number>) => v;
export const n7: number = ([{ [k!!]: v }]: Record<string, number>[]) => v;
export function f({ [k!!]: v }: Record<string, number>) { return v; }
export const n8: number = f;
const sentinel: number = "";
