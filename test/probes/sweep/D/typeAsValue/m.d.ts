declare module "m" {
    function test(): void;
    namespace test {
        interface TC { x: number }
    }
    export = test;
}
declare module "e" {
    export interface TE { x: number }
}
