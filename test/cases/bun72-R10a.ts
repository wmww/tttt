// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Primitive = string | number | boolean | null | undefined;
type DeepReadonly<T> = T extends Primitive ? T : { readonly [K in keyof T]: DeepReadonly<T[K]> };
class Store<S> { snapshot!: S }
class App extends Store<DeepReadonly<App>> { count = 1 }
class Other extends Store<Exclude<Other, null | undefined>> { count = 1 }
new App().snapshot.count = 2;
const sentinel: number = "";
