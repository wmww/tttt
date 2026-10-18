// Two types that print the same are named fully qualified, as tsc's getSymbolChain from no enclosing declaration
// finds them: from the globals (the namespace of the first file's UMD global `React` is `React`), else from their
// module, `import("/node_modules/react/index")` (the other copy's `export =` namespace stands for its module)
// @module: nodenext
// @filename: /node_modules/lib/node_modules/react/index.d.ts
export = React;
export as namespace React;
declare namespace React {
    type Node = string | number;
    type Ctor<P> = (props: P) => Node;
}
// @filename: /node_modules/lib/index.d.ts
import type { Ctor, Node } from "react";
export declare function render(wrapper: Ctor<{ children: Node }>): void;
// @filename: /node_modules/react/index.d.ts
export = React;
export as namespace React;
declare namespace React {
    type Node = string | bigint;
}
// @filename: /a.ts
import { render } from "lib";
import { type Node } from "react";
const wrapper = ({ children }: { children: Node }) => 1;
render(wrapper);
