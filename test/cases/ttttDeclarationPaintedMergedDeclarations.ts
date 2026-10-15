// A painted name declared by several statements (a namespace and the interface it merges with) has each of them
// walked: the interface's names paint their imports too (vscode's configureSnippets.d.ts lost `URI` and
// `IQuickPickItem`)
// @declaration: true
// @filename: /l.ts
export class URI { static isUri(x: any): x is URI { return true; } }
export interface Item { label: string }
// @filename: /c.ts
import { URI, Item } from "./l";
namespace Pick {
    export function is(thing: object | undefined): thing is Pick { return !!thing && URI.isUri((<Pick>thing).filepath); }
}
interface Pick extends Item { filepath: URI; }
export function promote(existing: Pick[]): void {}
// @filename: /d.ts
import { Item } from "./l";
interface Provider { item: Item }
declare function createDecorator<T>(name: string): { _: T };
const IService = createDecorator<IService>("s");
interface IService { register(provider: Provider): void; }
export class Main { constructor(service: IService) {} }
