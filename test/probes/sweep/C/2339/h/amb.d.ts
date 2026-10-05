declare module "m" {
    function m(): void;
    namespace m { function ok(): void; }
    export = m;
}
