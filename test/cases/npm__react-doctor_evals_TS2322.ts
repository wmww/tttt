// origin: npm @react-doctor/evals@0.0.0 https://github.com/millionco/react-doctor packages/evals/tsconfig.bench.json seed 20261305
// flip: yes
declare function fn<T extends (...args: any[]) => any>(impl: T): { (...a: Parameters<T>): ReturnType<T>; mockResolvedValue(v: Awaited<ReturnType<T>>): void };
const run = fn(async () => ({
  exitCode: 0,
  stdout: async () => "output",
  stderr: async () => "",
}));
run.mockResolvedValue({
  exitCode: 1,
  stdout: async () => "",
  stderr: async () => "build failed",
});
