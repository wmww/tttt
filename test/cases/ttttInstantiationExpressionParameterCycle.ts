// @strict: true
// @target: es2022
// A parameter typed through `Parameters<typeof f<T>>` of a function f's body calls: the conditional type's check
// (the permissive instantiation) must not infer f's return type, which would re-enter the signature being resolved
// (tttt reported `Expected 2-0 arguments, but got 4`, and on openclaw what the cycle left behind depended on the
// file a checker saw first)
export async function first() {
  return await runP("", 1, async (s) => s);
}
export async function withP<T>(owner: string, run: (s: number) => Promise<T>, options: { p?: boolean } = {}) {
  return await withStore(owner, (store) => runP(owner, store, run, options));
}
async function withStore<R>(o: string, f: (s: number) => Promise<R>): Promise<R> {
  return f(1);
}
async function runP<T>(owner: string, store: number, run: Parameters<typeof withP<T>>[1], options: { p?: boolean } = {}) {
  return await run(store);
}
export const x: Promise<string> = withP("", async () => "");
