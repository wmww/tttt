# Development
This is info about developing Cleat that isn't generally relevant to users of the library.

## Running tests
Simply run `./run-tests.sh`. This sets up your `build` directory (if needed) and runs tests under Valgrind. It also creates a symlink from the test directory to the project root, which is needed for `subdir()` to work in `test/meson.build`. If anything goes wrong first try `rm -rf build` and re-running.

## STC sub-tree
Cleat comes with [STC](https://github.com/stclib/STC) included in the repo. The least bad way I've found to vendor in a specific version of STC is using git subtree. Unlike submodules, cloning and using the repo just works. To see the STC commit that is checked out run:
```bash
git log --grep="git-subtree" --oneline
```
(the top line will show the current shortened STC commit hash).

To change the STC commit run:
```bash
git subtree pull --prefix=stc https://github.com/stclib/STC.git <HASH> --squash
```
