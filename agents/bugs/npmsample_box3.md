# Random npm projects found on the remote box "box3" (seeds 20261200-20261299)
Same conventions as agents/bugs/npmsample.md (one bullet per repro, root-cause guess, `[flip]` before the status tag
for flips); a separate file so the three boxes never conflict.

- Seed 20261200, auguwu/utils (@augu/utils@1.5.6, the repo's package.json is named `@noelware/utils`): spurious
  TS2307 on `import ... from '@noelware/utils'` in the package's own scripts. The package imports its own name, its
  `exports` target (dist/) isn't built, and a published copy of the same package sits in node_modules. tsgo's
  loadModuleFromSelfNameReference result is continueSearching when the export target doesn't resolve, so the
  lookup goes on into node_modules; tttt's load_from_self_name gets SEARCH_STOP from load_from_exports (which
  turns an unresolved target into a stop) and resolve_non_relative returns it. Repro
  test/cases/npm__augu_utils_TS2307.ts. Fixed (det3): load_from_self_name() now turns load_from_exports()' SEARCH_STOP into SEARCH_CONTINUE, as tsc's loadModuleFromSelfNameReference returns loadModuleFromExports' continueSearching (the node_modules walk already did). [fixed det3]
