// @strict: true
// @target: es2017
type D = { typed: true; value: number } | { typed: false; value: unknown };
interface S { run(x: number): Promise<D> }
const s: S = {
  run: async (x) => {
    return x > 0 ? { typed: true, value: 1 } : { typed: false, value: 'a' };
  },
};
const s2: S = {
  async run(x) {
    return { typed: false, value: 'a' };
  },
};
const s3: S = {
  async run(x) {
    return x > 0 ? { typed: true, value: 1 } : { typed: false, value: 'a' };
  },
};
