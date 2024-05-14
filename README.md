# DGED

Because there is room for one more minimalistic text editor!

DGED is what happens if I remove everything I do not need from
GNU Emacs and only implement the things I need to be productive.

The editor was created as a learning exercise and to create something that I
can use as a daily driver. If anyone else happens to find it useful I will
be very flattered and surprised.

DGED is in early development so bugs and breaking changes are to be
expected pre-1.0.0.

## Features

- [x] Syntax highlighting
- [x] TOML configuration file
- [x] Editor commands (M-x)
- [x] Absolutely no plugin system
- [x] Terminal only
- [x] Mouse-free editing
- [x] Naive and incorrect unicode handling
- [ ] LSP Client implementation

## Contributing

Contributions are of course welcome. Please open a PR on the Github repository.

## Development Setup

The editor is built using BSD make so that needs to be installed (preinstalled on BSD and bmake on Linux).

To enable syntax highlighting (default) you will also need the tree-sitter library
installed.

All of this can be obtained using the Nix flake. To create a development shell with all
needed dependencies, issue:

```
$ nix develop
```

Currently, tree-sitter grammars can only be automatically fetched when using the Nix setup.

To build the editor, first create the build folder

```
$ mkdir -p build
```

or

```
$ mkdir -p obj
```

Then call configure (see available options by passing `--help` to `./configure`)

```
$ ./configure
```

followed by (you guessed it!) make

```
$ bmake
```

## Installing

If using nix, installation can be done by referring the flake with for example `nix profile`:

```
$ nix profile install .
```

or by using `home-manager` or `configuration.nix`.

If not using Nix, obtain the needed dependencies and then issue

```
$ make
```

followed by

```
# make install
```

Optionally, you can set `prefix` (and `DESTDIR`) to a value of your liking.

## Documentation

The features of the editor are documented in the man page (`man dged`).
