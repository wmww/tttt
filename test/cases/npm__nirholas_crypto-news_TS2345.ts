// origin: npm @nirholas/crypto-news@1.0.1 https://github.com/nirholas/free-crypto-news tsconfig.bench.json seed 20261029
// flip: no
// unified's use<P>(plugin: Plugin<P, ...>, ...parameters: P | [boolean]) with rehype-highlight and { detect, ignoreMissing }: tsgo infers P from the arguments (no error), tttt from the plugin's parameters (TS2345, excess ignoreMissing)
// @allowJs: true
// @isolatedModules: true
// @jsx: react-jsx
// @lib: dom, dom.iterable, esnext
// @module: esnext
// @moduleResolution: bundler
// @resolveJsonModule: true
// @skipLibCheck: true
// @strict: true
// @target: es2022
// @types: node, react, react-dom, @testing-library/jest-dom, vitest/globals
// @esModuleInterop: true
// @preserveConstEnums: true
// @filename: node_modules/@testing-library/jest-dom/package.json
{"name": "@testing-library/jest-dom", "version": "7.0.1", "main": "dist/index.js", "module": "dist/index.mjs", "exports": {".": {"require": {"types": "./types/index.d.ts", "default": "./dist/index.js"}, "import": {"types": "./types/index.d.ts", "default": "./dist/index.mjs"}}, "./jest-globals": {"require": {"types": "./types/jest-globals.d.ts", "default": "./dist/jest-globals.js"}, "import": {"types": "./types/jest-globals.d.ts", "default": "./dist/jest-globals.mjs"}}, "./matchers": {"require": {"types": "./types/matchers-standalone.d.ts", "default": "./dist/matchers.js"}, "import": {"types": "./types/matchers-standalone.d.ts", "default": "./dist/matchers.mjs"}}, "./vitest": {"require": {"types": "./types/vitest.d.ts", "default": "./dist/vitest.js"}, "import": {"types": "./types/vitest.d.ts", "default": "./dist/vitest.mjs"}}, "./package.json": "./package.json"}, "types": "types/index.d.ts"}
// @filename: node_modules/@types/node/package.json
{"name": "@types/node", "version": "26.6.4", "main": "", "types": "index.d.ts", "typesVersions": {"<=5.6": {"*": ["ts5.6/*"]}, "<=5.7": {"*": ["ts5.7/*"]}}}
// @filename: node_modules/@types/react-dom/package.json
{"name": "@types/react-dom", "version": "19.3.0", "main": "", "types": "index.d.ts", "exports": {".": {"types": {"default": "./index.d.ts"}}, "./client": {"types": {"default": "./client.d.ts"}}, "./canary": {"types": {"default": "./canary.d.ts"}}, "./server": {"types": {"default": "./server.d.ts"}}, "./server.browser": {"types": {"default": "./server.browser.d.ts"}}, "./server.bun": {"types": {"default": "./server.bun.d.ts"}}, "./server.edge": {"types": {"default": "./server.edge.d.ts"}}, "./server.node": {"types": {"default": "./server.node.d.ts"}}, "./static": {"types": {"default": "./static.d.ts"}}, "./static.browser": {"types": {"default": "./static.browser.d.ts"}}, "./static.edge": {"types": {"default": "./static.edge.d.ts"}}, "./static.node": {"types": {"default": "./static.node.d.ts"}}, "./experimental": {"types": {"default": "./experimental.d.ts"}}, "./test-utils": {"types": {"default": "./test-utils/index.d.ts"}}, "./package.json": "./package.json"}}
// @filename: node_modules/@types/react/package.json
{"name": "@types/react", "version": "19.3.0", "main": "", "types": "index.d.ts", "typesVersions": {"<=5.0": {"*": ["ts5.0/*"]}}, "exports": {".": {"types@<=5.0": {"default": "./ts5.0/index.d.ts"}, "types": {"default": "./index.d.ts"}}, "./canary": {"types@<=5.0": {"default": "./ts5.0/canary.d.ts"}, "types": {"default": "./canary.d.ts"}}, "./compiler-runtime": {"types": {"default": "./compiler-runtime.d.ts"}}, "./experimental": {"types@<=5.0": {"default": "./ts5.0/experimental.d.ts"}, "types": {"default": "./experimental.d.ts"}}, "./jsx-runtime": {"types@<=5.0": {"default": "./ts5.0/jsx-runtime.d.ts"}, "types": {"default": "./jsx-runtime.d.ts"}}, "./jsx-dev-runtime": {"types@<=5.0": {"default": "./ts5.0/jsx-dev-runtime.d.ts"}, "types": {"default": "./jsx-dev-runtime.d.ts"}}, "./package.json": "./package.json"}}
// @filename: node_modules/rehype-highlight/package.json
{"exports": "./index.js", "name": "rehype-highlight", "type": "module", "version": "7.0.2"}
// @filename: node_modules/unified/package.json
{"name": "unified", "version": "11.0.5", "type": "module", "exports": "./index.js"}
// @filename: node_modules/vitest/package.json
{"name": "vitest", "type": "module", "version": "4.1.10", "imports": {"#module-evaluator": {"types": "./dist/module-evaluator.d.ts", "default": "./dist/module-evaluator.js"}, "#nodejs-worker-loader": "./dist/nodejs-worker-loader.js"}, "exports": {".": {"import": {"types": "./dist/index.d.ts", "default": "./dist/index.js"}, "require": {"types": "./index.d.cts", "default": "./index.cjs"}}, "./browser": {"types": "./browser/context.d.ts", "default": "./browser/context.js"}, "./package.json": "./package.json", "./optional-types.js": {"types": "./optional-types.d.ts"}, "./optional-runtime-types.js": {"types": "./optional-runtime-types.d.ts"}, "./src/*": "./src/*", "./globals": {"types": "./globals.d.ts"}, "./jsdom": {"types": "./jsdom.d.ts"}, "./importMeta": {"types": "./importMeta.d.ts"}, "./import-meta": {"types": "./import-meta.d.ts"}, "./node": {"types": "./dist/node.d.ts", "default": "./dist/node.js"}, "./internal/browser": {"types": "./dist/browser.d.ts", "default": "./dist/browser.js"}, "./runners": {"types": "./dist/runners.d.ts", "default": "./dist/runners.js"}, "./suite": {"types": "./dist/suite.d.ts", "default": "./dist/suite.js"}, "./environments": {"types": "./dist/environments.d.ts", "default": "./dist/environments.js"}, "./config": {"types": "./config.d.ts", "require": "./dist/config.cjs", "default": "./dist/config.js"}, "./coverage": {"types": "./coverage.d.ts", "default": "./dist/coverage.js"}, "./reporters": {"types": "./dist/reporters.d.ts", "default": "./dist/reporters.js"}, "./snapshot": {"types": "./dist/snapshot.d.ts", "default": "./dist/snapshot.js"}, "./runtime": {"types": "./dist/runtime.d.ts", "default": "./dist/runtime.js"}, "./worker": {"types": "./worker.d.ts", "default": "./dist/worker.js"}}, "main": "./dist/index.js", "module": "./dist/index.js", "types": "./dist/index.d.ts"}
// @filename: package.json
{"name": "free-crypto-news", "version": "1.0.10"}
// @filename: node_modules/@testing-library/jest-dom/types/index.d.ts
export {};
// @filename: node_modules/@types/node/index.d.ts
export {};
// @filename: node_modules/@types/react-dom/index.d.ts
export {};
// @filename: node_modules/@types/react/index.d.ts
export {};
// @filename: node_modules/rehype-highlight/index.d.ts
export { default } from "./lib/index.js";
// @filename: node_modules/rehype-highlight/lib/index.d.ts
/**
 */
export default function rehypeHighlight(options?: Readonly<Options> | null | undefined): (tree: Root, file: VFile) => undefined;
/**
 */
export type Options = {
    /**
     */
    detect?: boolean | null | undefined;
    /**
     */
};
// @filename: node_modules/unified/index.d.ts
export {unified} from './lib/index.js'
/**
 */
// @filename: node_modules/unified/lib/callable-instance.d.ts
export const CallableInstance: new <Parameters extends unknown[], Result>(property: string | symbol) => (...parameters: Parameters) => Result;
// @filename: node_modules/unified/lib/index.d.ts
import {CallableInstance} from './callable-instance.js'
/**
 */
export class Processor<ParseTree extends import("unist").Node | undefined = undefined, HeadTree extends import("unist").Node | undefined = undefined, TailTree extends import("unist").Node | undefined = undefined, CompileTree extends import("unist").Node | undefined = undefined, CompileResult extends CompileResults | undefined = undefined> extends CallableInstance<[], Processor<ParseTree, HeadTree, TailTree, CompileTree, CompileResult>> {
    use<Parameters_1 extends unknown[] = [], Input extends string | import("unist").Node | undefined = undefined, Output = Input>(plugin: Plugin<Parameters_1, Input, Output>, ...parameters: Parameters_1 | [boolean]): UsePlugin<ParseTree, HeadTree, TailTree, CompileTree, CompileResult, Input, Output>;
}
export const unified: Processor<undefined, undefined, undefined, undefined, undefined>;
export type Node = import('unist').Node;
export type Plugin<PluginParameters extends unknown[] = [], Input extends string | import("unist").Node | undefined = import("unist").Node, Output = Input> = ((this: Processor, ...parameters: PluginParameters) => Input extends string ? Output extends Node | undefined ? undefined | void : never : Output extends CompileResults ? Input extends Node | undefined ? undefined | void : never : Transformer<Input extends Node ? Input : Node, Output extends Node ? Output : Node> | undefined | void);
/**
 */
// @filename: node_modules/vitest/globals.d.ts
export {};
// @filename: src/lib/docs.ts
/**
 */
  const [{ unified }, remarkParse, remarkGfm, remarkRehype, rehypeSlug, rehypeHighlight, rehypeStringify] =
    await Promise.all([
      import('unified'),
      import('remark-parse'),
      import('remark-gfm'),
      import('remark-rehype'),
      import('rehype-slug'),
      import('rehype-highlight'),
    ]);
  const processed = await unified()
    .use(rehypeHighlight.default, { detect: true, ignoreMissing: true })
/**
 */
