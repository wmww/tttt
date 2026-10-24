// origin: npm rabbitmq-pub-sub@0.2.5 https://github.com/cdmbase/rabbitmq-pubsub tsconfig.bench.json seed 20261106
// flip: no
// @module: commonjs
// @moduleResolution: bundler
// @target: es6
// @strict: true
// @filename: node_modules/bb/index.d.ts
declare class Bluebird<R> {
    static resolve<R>(value: R): Bluebird<R>;
}
declare namespace Bluebird {
    interface Inspection<R> {}
}
export = Bluebird;
// @filename: node_modules/bb/package.json
{"name":"bb","types":"index.d.ts"}
// @filename: a.ts
import * as Promise from "bb";
export function create(): Promise<number> {
    return Promise.resolve(1);
}
