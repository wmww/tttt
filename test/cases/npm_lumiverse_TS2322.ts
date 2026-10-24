// origin: npm lumiverse-backend@1.2.5 https://github.com/prolix-oc/Lumiverse tsconfig.bench.json seed 20261302
// flip: yes
// @strict: true
// @target: esnext
export function reorder(userId: string, ids: string[]): void {
  return finish(mutate(getDb(), { action: "reorder", args: [userId, ids] }), userId);
}
function finish<T>(result: Result, userId: string): T {
}
