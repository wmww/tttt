// origin: npm @akashnetwork/console-web@3.31.0 https://github.com/akash-network/console apps/deploy-web/tsconfig.bench.json seed 20261304
// flip: no
// @strict: true
// @target: es2022
// @filename: app-di-container.ts
import { createContainer } from "./createContainer";
  const container = createContainer({
    applyAxiosInterceptors: (): typeof withInterceptors => {
    },
    stripe: () =>
      new HttpStripeService(
      ),
    stripeService: () =>
      new StripeService({
      }),
    providerProxy: () => {
      return new ProviderProxyService(
        container.applyAxiosInterceptors(container.createAxios({ baseURL: "/" }), {
          request: [config => ({ ...config, baseURL: getBaseUrl() })]
        }),
      );
    },
    deploymentSetting: () =>
      container.applyAxiosInterceptors(new DeploymentSettingHttpService(apiConfig), {
      }),
    apiKey: () =>
      container.applyAxiosInterceptors(new ApiKeyHttpService(apiConfig), {
      }),
    createAxios:
        createHttpClient(options),
  });
export function withInterceptors<T extends Axios | AxiosInstance = AxiosInstance>(axios: T, interceptors?: Interceptors) {
}
type Interceptor<T> = (value: T) => T | Promise<T>;
interface Interceptors {
  request?: Array<Interceptor<InternalAxiosRequestConfig> | undefined>;
}
// @filename: createContainer.ts
export function createContainer<T extends Factories>(factories: T): DIContainer<T> {
}
export type DIContainer<T extends Factories> = {
  [K in keyof T]: ReturnType<T[K]>;
};
export type Factories = Record<string, () => any>;
