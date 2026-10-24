// origin: npm @akashnetwork/console-web@3.31.0 https://github.com/akash-network/console apps/deploy-web/tsconfig.bench.json seed 20261304
// flip: no
// @strict: true
// @target: es2022
// A DI container whose factories read the container being declared: each `container.x(...)` callee is checked behind
// its call's barrier (tsc's getResolvedSignature, first resolution), so the declaration in flight is computed again,
// nested, rather than a cycle; the second factory's reference comes inside the first re-entry (cut down by hand from
// the reduction, which kept unresolved names)
type Factories = Record<string, () => any>;
type DIContainer<T extends Factories> = { [K in keyof T]: ReturnType<T[K]> };
declare function createContainer<T extends Factories>(factories: T): DIContainer<T>;
declare function withInterceptors<T>(axios: T, interceptors?: { request?: Array<(v: string) => string> }): T;
const container = createContainer({
  apply: (): typeof withInterceptors => withInterceptors,
  proxy: () => {
    return container.apply(container.make({}), { request: [config => config + "!"] });
  },
  setting: () => container.apply(1),
  make: () => (o: object) => "x",
});
