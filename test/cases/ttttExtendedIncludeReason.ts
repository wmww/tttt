// An include spec inherited from an extended config is shown rebased onto the extending config's directory
// (combinePaths, not normalized) in the "Matched by include pattern" reason
// @filename: tsconfig.json
{
    "extends": "./configs/base.json",
    "compilerOptions": {
        "rootDir": "./src2"
    }
}
// @filename: configs/base.json
{
    "include": ["../src1/*"]
}
// @filename: src1/main.ts
export const x = 1;
