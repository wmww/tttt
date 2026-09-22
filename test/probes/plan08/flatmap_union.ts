// @strict: true
interface Tag { tag: 1 }
interface NodeArray<T> extends ReadonlyArray<T> { pos: number }
interface Doc { kind: "doc"; tags: NodeArray<Tag> }
declare function isDoc(x: Doc | Tag): x is Doc;
declare function flatMap<T, U extends {}>(array: readonly T[] | undefined, mapfn: (x: T, i: number) => U | readonly U[] | undefined): readonly U[];
declare const comments: (Doc | Tag)[];
let tags: readonly Tag[] | undefined;
tags = flatMap(comments, j => isDoc(j) ? j.tags : j);
