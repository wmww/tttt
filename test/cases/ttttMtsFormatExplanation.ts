// A .mts file's format comes from its extension: tsc's file explanation doesn't add "File is ECMAScript module
// because package.json has field type" for it, unlike for the .ts next to it
// @filename: tsconfig.json
{
    "compilerOptions": {
        "module": "nodenext",
        "rootDir": "./src"
    },
    "include": ["./scripts"]
}
// @filename: package.json
{ "type": "module" }
// @filename: scripts/a.mts
export const a = 1;
// @filename: scripts/b.ts
export const b = 1;
// @filename: scripts/c.cts
export const c = 1;
