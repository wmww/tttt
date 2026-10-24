// origin: sourcesweep-style corpus vue https://github.com/vuejs/core tsconfig.corpus.json (packages/compiler-sfc/src/style/pluginScoped.ts:190)
// flip: no
// postcss-selector-parser's `type Selector = _Selector<Selector>` with `interface _Selector<S> extends Container<string, Diff<Node, S>>`: tsgo hits
// its instantiation depth limit (TS5115) in a chain getConditionalType -> isTypeAssignableTo -> isWeakType ->
// resolveObjectTypeMembers of a reference -> its base's type arguments -> Diff again, and the errorType lands in
// Pseudo's members: Pseudo.nodes is any[] for good, so n.nodes[0].each is any and its callback's ss is TS7006.
// tttt reports the TS5115 too but its error lands elsewhere: Pseudo.nodes stays Selector[] (bugs/npmsample.md)
// @strict: true
// @esModuleInterop: true
// @module: esnext
// @moduleResolution: bundler
// @filename: node_modules/psp/package.json
{"name":"psp","version":"1.0.0","types":"index.d.ts"}
// @filename: node_modules/psp/index.d.ts
export = parser;
type Diff<T, U> = T extends U ? never : T;
declare namespace parser {
    interface NodeTypes {
        root: Root,
        pseudo: Pseudo,
    }
    type Node = NodeTypes[keyof NodeTypes];
    class Processor<
    > {
    }
    interface Container<
        Value extends string | undefined = string,
        Child extends Node = Node
    > extends Base<Value> {
        nodes: Array<Child>;
        each(callback: (node: Child, index: number) => boolean | void): boolean | undefined;
    }
    interface Root extends Container<undefined, Selector> {
    }
    interface _Selector<S> extends Container<string, Diff<Node, S>> {
        type: "selector";
    }
    type Selector = _Selector<Selector>;
    interface Pseudo extends Container<string, Selector> {
        type: "pseudo";
    }
    /**
     */
}
// @filename: w.ts
import selectorParser from 'psp'
export function rewriteSelector(selector: selectorParser.Selector) {
  selector.each(n => {
    if (n.type === 'pseudo') {
      n.nodes[0].each(ss => {})
    }
  })
}
declare const p: selectorParser.Pseudo;
export const pn = p.nodes;
declare const r: selectorParser.Root;
export const rn = r.nodes;
