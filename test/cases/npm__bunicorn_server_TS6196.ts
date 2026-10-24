// origin: npm @bunicorn/server@0.0.30 https://github.com/ragokan/bunicorn packages/server/tsconfig.json seed 20261202
// flip: no
// @strict: true
// @noUnusedLocals: true
// @noUnusedParameters: true
// @noEmit: true
// A type reference to a namespace with type arguments (`Serve<T>`, TS2709): tsgo still checks the type arguments,
// which marks the type parameter T referenced; tttt reports T unused (spurious TS6196).
// @filename: bun.d.ts
declare module "bun" {
    namespace Serve {
        interface Options { port?: number }
    }
    interface ServeOptions { hostname?: string }
}
// @filename: a.ts
import type { Serve, ServeOptions } from "bun";
export class App {
    serve<T>(options: Omit<ServeOptions & Serve<T>, "fetch">): void {}
}
