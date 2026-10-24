// origin: npm serverless-openapi@1.1.2 https://github.com/theodo/serverless-openapi tsconfig.bench.json seed 20261036
// flip: no
// @lib: es6, es7, dom, es2018.asynciterable
// @module: commonjs
// @moduleResolution: bundler
// @noUnusedLocals: true
// @target: es6
// @useDefineForClassFields: false
// @filename: node_modules/json-schema-ref-parser/package.json
{"name": "json-schema-ref-parser", "version": "6.1.0", "main": "lib/index.js", "typings": "lib/index.d.ts"}
// @filename: package.json
{"name": "serverless-openapi", "version": "1.1.2", "main": "index.js"}
// @filename: node_modules/json-schema-ref-parser/lib/index.d.ts
export = $RefParser
/**
 */
declare class $RefParser {
  static bundle(schema: string | $RefParser.JSONSchema, callback: $RefParser.SchemaCallback): void;
  /**
   */
}
// @filename: src/parse.ts
import * as $RefParser from "json-schema-ref-parser";
export async function parseModels(
): Promise<{}> {
    const schema = (typeof model.schema === "string"
      ? await $RefParser.bundle(path.resolve(root, model.schema))
      : model.schema) as JSONSchema7;
}
