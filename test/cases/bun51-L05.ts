// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Call { name: string; arguments: string }
/**
 * Runs a tool call.
 * @param call the call
 */
export const handle = (call: Call) => {
  const { name, arguments: args } = call;
  return [name, args];
};
export class K {
  /** @param call the call */
  handle = (call: Call) => {
    const { arguments: args } = call;
    return args;
  };
}
/** @param z */
export function j(z: number) { const arguments = 1; return z; }
const sentinel: number = "";
