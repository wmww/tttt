// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
type Action = { kind: 'A', payload: number } | { kind: 'B', payload: string };
function f1({ kind, payload }: Action) { var kind: 'A' | 'B'; if (kind === 'A') { payload.toFixed(); } }
const sentinel: number = "";
