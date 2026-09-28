// @strict: true
type ByObject = { [p: string]: { [p: string]: any } };
function f<T extends object>(byObj: ByObject, v: string) {
    const o = byObj[v as keyof (keyof T)];
    const w = v as keyof (keyof T);
}
