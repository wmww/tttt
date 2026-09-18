# Cleat
Cleat (C Language Extensions And Tools) is a modern standard library for C. APIs are very unstable, documentation is WIP, see [docs](docs/) for what I've written so far.

## Using in your project
`setup-example.sh` can be copied into your project as `setup.sh` to manage this and other dependencies. You could also use git subtrees, submodules, or any other way of obtaining the code.

### Headers
`cleat.h` pulls in the most common headers (display, failable, panic, etc). It's recommended for source files and projects that are using several cleat's features. It defines a lot of macros and generally pollutes the namespace, so it may not be best to use in headers that you expect third parties to include. For those use the individual headers in the `cleat/` subdirectory, and the `_minimal` versions where possible.

### Meson
To use Cleat from meson run `subdir('.../cleat')` and then add `dependencies: [cleat_dep]` to your executable/library target. This will pull in both Cleat and the STC it comes vendored with. You can also add `c_args: cleat_c_args` if you want to enable the default compiler warnings/errors in your project (recommended).

### Stability
Cleat is not stable in any way. It has no versions yet, and major breaking API changes are to be expected. It is recommended that you use a specific commit of Cleat and only change that commit when you're ready to fix and upgrade things.

## License
Both Cleat and STC (which it comes with) are licensed under MIT.
