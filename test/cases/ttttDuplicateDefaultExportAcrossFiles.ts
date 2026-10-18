// Conflicting default exports of an ambient module declared in several files: each conflict names every declaration
// by the symbol merged in (tsc's reportMergeSymbolError, symbolToString(source)), the later file's
// @filename: a.d.ts
declare module '*.png' {
    const content: string
    export default content
}
// @filename: b.d.ts
declare module '*.png' {
    const src: string
    export default src
}
// @filename: c.d.ts
declare module '*.png' {
    const img: string
    export default img
}
