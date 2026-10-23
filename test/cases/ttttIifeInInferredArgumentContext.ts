// @strict: true
// @noEmit: true

// An immediately invoked function inside an argument of a call inferring now: asking for its return type there must
// not check its context root (the return expression of `resolve`) out of context, which would leave `err` without a
// contextual signature for good (TS7006). The callee of a call has no contextual type: it's context-checked without one
type Rec = { update?: { key: string; pin?: { owner: string } }; onErr?: (err: unknown) => void };
type Turn<R> = { result?: R; rec: Rec };
type Params<T, R> = { raw: T; adapter: { ingest: () => { raw: T }; resolve: () => Turn<R> | Promise<Turn<R>> } };
declare function run<T, R = string>(p: Params<T, R>): void;

run({
  raw: 1,
  adapter: {
    ingest: () => ({ raw: 1 }),
    resolve: () => ({
      rec: {
        update: { key: "k", pin: (() => { return undefined; })() },
        onErr: (err) => { String(err); },
      },
    }),
  },
});

run({
  raw: 1,
  adapter: {
    ingest: () => ({ raw: 1 }),
    resolve: () => ({
      result: 1,
      rec: { update: { key: "k", pin: (() => ({ owner: "o" }))() }, onErr: (err) => { String(err); } },
    }),
  },
});
