// npm sample 20261023 zooqle-api-ts: a declaration destructuring a union with null reads the declared type
// @target: es2015
// @filename: index.ts
declare const s: string
function f() {
    let [, a, b]: string[] | number[] = s.match(/"(.+)"/)
    b = parseInt(a, 10)
}
declare const o: { x: string | number } | undefined
function g() {
    let { x }: { x: string | number } = o
    x.length
    let [y]: (string | number)[] = [""]
    y.length
}
