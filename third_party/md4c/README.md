# md4c

Vendored copy of [md4c](https://github.com/mity/md4c) v0.5.2 (`release-0.5.2`).

Only the core parser is included: `md4c.c`, `md4c.h`, `entity.c`, `entity.h`.
The HTML/XML renderers and the CLI are not vendored; `src/doc.cpp` implements
its own `MD_PARSER` callbacks that build a block tree.

Licensed under the MIT license, see the header of `md4c.c`.
