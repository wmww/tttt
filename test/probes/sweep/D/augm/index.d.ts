declare module "outer" {
    import * as lib from "missing-lib";
    module "missing-lib" {
        interface TFunction { (key: string): string }
    }
    interface P extends lib.PostProcessorModule { name: string }
}
