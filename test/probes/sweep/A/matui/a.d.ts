declare namespace NS {
    export class AppBar {}
}
declare module "m" {
    import X = NS.AppBar;
    export default X;
}
