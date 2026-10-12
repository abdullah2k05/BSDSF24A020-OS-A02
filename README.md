# BSDSF24A020-OS-A02

**Operating Systems — Programming Assignment 02: Re-engineering the `ls` Utility**

A from-scratch C implementation of the Unix `ls` command, evolved through seven
versioned releases. Each release lives on its own feature branch, is merged back
into `main`, and is published as an annotated tag plus a GitHub Release with the
compiled binary attached.

## Project Structure

```
BSDSF24A020-OS-A02/
├── src/
│   └── ls-v1.0.0.c     # the single source file, extended at each stage
├── bin/                # compiled program (bin/ls)
├── obj/                # object files
├── man/                # reserved for the bonus man page
├── Makefile            # build automation
├── README.md
└── REPORT.md           # answers to the report questions
```

## Build

```bash
make          # builds ./bin/ls
make run      # builds and runs with no arguments (lists pwd)
make clean    # removes bin/ls and obj/*.o
```

## Usage

```bash
./bin/ls               # default: down-then-across column listing
./bin/ls -l            # long listing format
./bin/ls -x            # horizontal (left-to-right) column listing
./bin/ls -R            # recursive listing
./bin/ls -lR src       # combinations work too
```

## Version History

| Version | Feature | Branch | Tag |
|---------|---------|--------|-----|
| 1.0.0 | Project setup & initial build | `main` | — |
| 1.1.0 | Long listing format (`-l`) | `feature-long-listing-v1.1.0` | `v1.1.0` |
| 1.2.0 | Column display (down then across) | `feature-column-display-v1.2.0` | `v1.2.0` |
| 1.3.0 | Horizontal column display (`-x`) | `feature-horizontal-display-v1.3.0` | `v1.3.0` |
| 1.4.0 | Alphabetical sort (`qsort`) | `feature-alphabetical-sort-v1.4.0` | `v1.4.0` |
| 1.5.0 | Colorized output | `feature-colorized-output-v1.5.0` | `v1.5.0` |
| 1.6.0 | Recursive listing (`-R`) | `feature-recursive-listing-v1.6.0` | `v1.6.0` |

## Color Scheme (v1.5.0+)

| Color | File type |
|-------|-----------|
| Blue | Directory |
| Green | Executable |
| Red | Archive (`.tar`, `.gz`, `.zip`, ...) |
| Pink/Magenta | Symbolic link |
| Reverse video | Special file (device, socket, FIFO) |

## Author

- **Roll No:** BSDSF24A020
