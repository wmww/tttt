declare const p: { stderr: number } & Promise<{ stderr: string }>;
async function f() {
  const r = await p;
  r.stderr.split("");
}
