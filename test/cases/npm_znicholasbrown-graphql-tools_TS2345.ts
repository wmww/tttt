// origin: npm znicholasbrown-graphql-tools@4.0.6 https://github.com/apollographql/graphql-tools tsconfig.bench.json seed 20261104
// flip: yes
// @strict: true
export function f<
  TArgs extends any[],
  TSync extends (...args: TArgs) => any,
  TAsync extends (...args: TArgs) => Promise<any>,
>(executeSync: TSync, executeAsync: TAsync) {
  return (...args: Parameters<TSync | TAsync>) => {
    executeSync(...args);
  };
}
