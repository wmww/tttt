// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
type Hydrated<T> = T extends { x: {} }
  ? T
  : T & {
      [P in keyof T]: T[P] extends Lazy<infer U> ? Lazy<U> : T[P];
    };

declare class Lazy<T> {
  brand: T;
}

declare class Model {
  isHydrated(): this is {};
  assertHydrated(): this is Hydrated<this>;
}
declare class Collection<T> {
  hydrate(): Hydrated<T>;
}
class ChildCollection extends Collection<Model> {}

declare function relation<T extends Model>(ctor: new () => Collection<NoInfer<T>>): void;

relation(ChildCollection);
const sentinel: number = "";
