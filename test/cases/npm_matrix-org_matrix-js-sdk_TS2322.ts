// origin: npm matrix-org/matrix-js-sdk@develop https://github.com/matrix-org/matrix-js-sdk tsconfig.json seed 20261401
// flip: yes
// @strict: true
// @noEmit: true
// @module: esnext
// @moduleResolution: bundler
// A module augmentation adds an overload to a class's method through a merged interface: the package's class has
// `to_json(): unknown`, the augmentation `to_json(): Promise<Json>`. tsgo resolves the call to the augmentation's
// signature (the later declaration's overloads come first); tttt takes the class's `unknown` (spurious TS2322).
// @filename: node_modules/wasm/package.json
{"name": "wasm", "version": "1.0.0", "types": "index.d.ts"}
// @filename: node_modules/wasm/index.d.ts
export class SecretsBundle {
    to_json(): unknown;
}
// @filename: src/types.d.ts
import type * as Wasm from "wasm";
type SecretsBundleJson = { cross_signing: { master_key: string } };
declare module "wasm" {
    interface SecretsBundle {
        to_json(): Promise<SecretsBundleJson>;
    }
}
// @filename: src/a.ts
import { SecretsBundle } from "wasm";
type Json = { cross_signing: { master_key: string } };
export async function f(b: SecretsBundle): Promise<Json> {
    const secrets = b.to_json();
    return secrets;
}
