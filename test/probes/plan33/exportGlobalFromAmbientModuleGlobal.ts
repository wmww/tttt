// @filename: a.d.ts
declare module "m" {
    global { type G = number; }
    export { type G };
}
// @filename: b.d.ts
type H = string;
declare module "n" {
    export { type H };
}
