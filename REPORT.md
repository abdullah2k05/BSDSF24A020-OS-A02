# Programming Assignment 02 — Report

**Student:** Abdullah — BSDSF24A020
**Course:** Operating Systems (OS-A02)
**Instructor:** Muhammad Arif Butt, PhD
**Repository:** https://github.com/abdullah2k05/BSDSF24A020-OS-A02

All answers below refer to the actual implementation in `src/ls-v1.0.0.c`
(line numbers refer to the final v1.6.0 source).

---

## Feature-2: ls-v1.1.0 — Complete Long Listing Format

### Q1. What is the crucial difference between the `stat()` and `lstat()` system calls? In the context of the `ls` command, when is it more appropriate to use `lstat()`?

Both calls fill the same `struct stat`, but they differ in what they describe when the
path names a **symbolic link**:

| | `stat(path, &st)` | `lstat(path, &st)` |
|---|---|---|
| Resolves symlinks? | **Yes** — follows the link chain to the final target | **No** — stops at the link itself |
| On a symlink `a -> b` | returns `b`'s type, size, inode, permissions | returns `l` type, link's own size (length of the target string), link's own inode/mtime |
| Cost | can be many extra lookups; can fail if the target does not exist | one lookup, never traverses |
| On a non-symlink | identical | identical |

**When to use `lstat()`:** whenever you are reporting on an entry *as it appears in the
directory*, which is exactly what `ls -l` does. `ls -l` must print `lrwxrwxrwx` for a
link — not the target's `drwxr-xr-x` — and must print the link's own size and its
own modification time, followed by ` -> target` obtained separately with `readlink()`.
That is precisely why `print_long_entry()` (line 253) and the width-discovery pass
(line 548) both call `lstat()`. Using `stat()` there would silently turn every
symlink line into a line about its target: `readlink`'s arrow would be appended to a
target description, hard-link counts would be the target's, and a *broken* symlink
would become an unreadable error instead of a normal listing entry.

**When `stat()` is the right choice:** when you need to know about the *destination*.
In this program `list_operand()` (line 610) and the operand-scan in `main()` (line
706) use `stat()`, because a command-line operand behaves like a shell word: the
question "is `foo` a directory I should descend into?" is answered by following the
link, so `ls mylink_to_dir` lists the directory's contents, exactly like GNU `ls`.

The third place matters too: the `-R` walk (line 586) uses `lstat()` deliberately so
a symlink pointing at a directory is **not** followed — otherwise a self-referential
or cross-linked symlink tree would make the recursion run forever.

### Q2. The `st_mode` field in `struct stat` is an integer that contains both the file type and the permission bits. Explain how you can use bitwise operators (like `&`) and predefined macros (like `S_IFDIR` or `S_IRUSR`) to extract this information.

`st_mode` is a `mode_t` (an unsigned integer) packed as:

```
bits 15..12   bits 11..9        bits 8..0
 file type      setuid/setgid/    rwx rwx rwx
 (S_IFMT)       sticky            owner group other
```

* **High bits = file type.** The `S_IFMT` mask (`0170000`) isolates them. The
  predefined constants are the *expected values*, not masks: `S_IFDIR` is
  `0040000`, `S_IFLNK` is `0120000`, `S_IFREG` is `0100000`. So the type test is
  a masked comparison:

  ```c
  (st.st_mode & S_IFMT) == S_IFDIR     /* is it a directory? */
  ```

  The `<sys/stat.h>` headers wrap this idiom in the type-predicate macros
  `S_ISDIR(m)`, `S_ISLNK(m)`, `S_ISCHR(m)`, `S_ISBLK(m)`, `S_ISFIFO(m)`,
  `S_ISSOCK(m)` — each of which expands to exactly that masked comparison.
  `mode_to_string()` (lines 75–81) uses them to pick the leading letter of the
  permission string: `d`, `l`, `c`, `b`, `p`, `s`, or `-` for a regular file.

* **Low bits = permissions.** Each of the nine permission bits already has its own
  single-bit macro, so a simple `&` against it isolates one bit:

  ```c
  if (mode & S_IRUSR)   /* 0400 — owner-readable?  */
      str[1] = 'r';
  else
      str[1] = '-';
  ```

  Because the mask is a power of two, the result is `0` (bit clear) or the mask
  itself (bit set) — both are unambiguous in a boolean context, which is what
  lets `mode_to_string()` (lines 83–96) write the nine characters
  `rwxr-xr--` one at a time. `S_ISUID` (`04000`), `S_ISGID` (`02000`) and
  `S_ISVTX` (`01000`) are combined with the corresponding execute bit to render
  `s`/`S` and `t`/`T` correctly.

* **Combining masks.** A mask of several bits tells you whether *any* of them is
  set: `mode & 0111` (line 204) is non-zero when the file is executable by owner,
  group **or** others — the test used by the colouriser.

* **Extracting a numeric value.** Shifting recovers the octal digit:

  ```c
  (mode >> 6) & 07      /* owner's rwx as 0..7 */
  ```

  We never need to do this, because printing char-by-char is clearer.

---

## Feature-3: ls-v1.2.0 — Column Display (Down Then Across)

### Q1. Explain the general logic for printing items in a "down then across" columnar format. Why is a simple single loop through the list of filenames insufficient?

"Down then across" (column-major) means the first `num_rows` entries go into
column 1 top-to-bottom, the next `num_rows` into column 2, and so on. For entry
index `i` (0-based, in sorted order):

```
row = i % num_rows
col = i / num_rows
```

That is the whole reason a plain `for (i = 0; i < count; i++) printf("%s ", names[i])`
is wrong: **a single sequential loop emits items in index order, which is
row-major** — it produces `a b c` on line 1, `d e f` on line 2, i.e. "across then
down", not the layout the assignment asks for.

To print column-major you must invert the iteration: you have to iterate over
*rows* in the outer loop and *columns* in the inner loop, and compute the index
from `(row, col)` rather than the other way round. That is the structure of
`print_columnar()` (lines 372–394):

```c
for (row = 0; row < num_rows; row++)
    for (col = 0; col < num_cols; col++) {
        size_t idx = col * num_rows + row;   /* invert: (row,col) -> index */
        if (idx >= count) continue;          /* short last column */
        put_name(dir, names[idx]);
        printf("%*s", span - len, "");       /* pad to this column's width */
    }
```

Two further things a single pass cannot do:

1. **Column widths are not known until the whole array is known.** Each column is
   as wide as its *own* widest entry (`column_span()`, lines 304–320), and an
   entry may sit in any column depending on `num_rows`. So a pre-computation pass
   is mandatory before the first line can be printed.
2. **The layout itself must be chosen first.** `num_rows` is derived from the
   terminal width and the entry count (lines 356–370) and then refined by
   `layout_fits()`; printing as you read the directory would mean you had not yet
   decided how many rows there were.

Also note that the *last* column is usually short: `idx >= count` skips those
holes, and `last_col` (lines 374–377) suppresses the trailing padding on the final
printed column of each row so no line ends in whitespace.

### Q2. What is the purpose of the `ioctl` system call in this context? What would be the limitations of your program if you only used a fixed-width fallback (e.g., 80 columns) instead of detecting the terminal size?

`ioctl()` is the generic "control operations on a file descriptor" system call — a
catch-all for requests that do not fit the read/write model. Here it is called
with the request **`TIOCGWINSZ`** ("get window size"), whose payload is a
`struct winsize` that the kernel fills in:

```c
static int get_terminal_width(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return (int)ws.ws_col;
    return 80;            /* fallback when not a terminal */
}
```
(lines 294–301)

Its purpose is to make the layout *responsive*: the number of columns the display
can have is `terminal_width / (longest_name + 2)` (line 356), and the wrap point
for `-x` is the same number (line 420). The kernel knows the window dimensions
because the terminal driver tracks them (and is updated by the window manager or
by `SIGWINCH`), so `ioctl` is the one cheap, correct way to ask.

`ioctl` **can fail** — `stdout` may be a pipe or a file (`ENOTTY`), or `ws_col`
may be 0 (a pty that has not been sized yet). That is why the fallback exists: it
keeps the program working in non-interactive contexts such as `ls | less` or a
shell script.

Limitations of an **unconditional 80-column** assumption:

* **Narrow terminals (e.g. 40 columns):** the computed lines are up to twice as
  wide as the screen. The terminal wraps them, so every visual row becomes two or
  more physical rows and the carefully computed column alignment is destroyed —
  the listing no longer looks like a table at all.
* **Wide terminals (e.g. 200 columns):** everything fits, but only ~10% of the
  width is used; the output is a tall narrow ribbon instead of a wide block,
  which is exactly what columnar output exists to avoid.
* **`-x` mode degrades most visibly:** with a fixed 80 the wrap decision at
  line 420 is simply wrong, so names get truncated by the terminal's own
  wrapping.
* It also breaks the `ioctl`-based behaviour users expect from real `ls`, and
  means a test harness that resizes the pty cannot observe any change.

Hence: try `ioctl`, fall back to 80 only when the kernel says "I can't tell you".

---

## Feature-4: ls-v1.3.0 — Horizontal Column Display (`-x`)

### Q1. Compare the implementation complexity of the "down then across" (vertical) printing logic versus the "across" (horizontal) printing logic. Which one requires more pre-calculation and why?

**The vertical layout requires far more pre-calculation.**

**Horizontal (`-x`), `print_horizontal()` (lines 406–443)** is a greedy,
single-pass, online algorithm. It needs exactly one global fact up front — the
uniform column width `max_len + 2` — and then walks the array once, keeping a
cursor `pos`:

```c
for (i = 0; i < count; i++) {
    if (pos > 0 && pos + len > term_width) { putchar('\n'); pos = 0; }
    put_name(dir, names[i]);
    pos += len;
    ... pad to the next column start, or force a wrap ...
}
```

It never has to look ahead: if the next name would not fit, end the line now and
keep going. Decision-making is local, constant-memory, and one pass.

**Vertical (default), `print_columnar()` (lines 348–395)** must decide the
*entire* geometry before printing anything, because every line's content depends
on it:

1. **Choose `num_rows`/`num_cols`.** Start from the assignment's rule
   `num_cols = term_width / (max_len + 2)`, take the ceiling
   `num_rows = ceil(count / num_cols)`.
2. **Then tighten.** Real column widths are dictated by *which* names land in
   *which* column, so the estimate from step 1 can be pessimistic. The loop at
   lines 366–368 repeatedly asks `layout_fits(num_rows - 1, ...)` — i.e. "could
   we have one fewer row, hence one more column?" — and shrinks `num_rows` while
   the answer stays yes. This is what makes GNU `ls` fill the screen.
3. **`layout_fits()` itself needs `column_span()`,** which scans all entries that
   would occupy a column to find that column's maximum (lines 304–320). So each
   candidate layout costs a full O(count) sweep, and step 2 may do several of
   them.
4. **Column widths are per-column, not global.** The pad width of column `c` is
   `column_span(c, num_rows, ...) + 2`, recomputed at print time; and the index
   must be inverted as `idx = col * num_rows + row`.

So: vertical = *plan the whole grid, then emit* (multi-pass, O(count) per
candidate layout); horizontal = *walk and wrap* (single pass, O(count) total).
The extra complexity in the vertical case is not in the printing loop — it is in
**layout selection**, because column-major filling couples the geometry decision
to the data that has not been printed yet. In horizontal mode the geometry is
data-independent (one width for every column), so no such search is needed.

A secondary cost of vertical mode: it must handle the ragged last column
(`idx >= count`), suppress padding after the last printed column of each row
(`last_col`, lines 374–377), and cope with `max_len + 2 > term_width` by
clamping to one column (lines 357–358). Horizontal mode handles the same
situation with the single condition `next_col <= term_width`.

### Q2. Describe the strategy you used in your code to manage the different display modes (`-l`, `-x`, and default). How did your program decide which function to call for printing?

The strategy is **"one mode variable, set once by `getopt`, dispatched once by
`do_ls()`."**

**Step 1 — represent the mode as an enum, not as a set of booleans.**

```c
enum display_mode { MODE_VERTICAL, MODE_LONG, MODE_HORIZONTAL };
```

`main()` initialises `enum display_mode mode = MODE_VERTICAL;` (line 657). A
single enum makes the three modes mutually exclusive by construction: it is
impossible to be in `-l` and `-x` at the same time, which is exactly the GNU `ls`
semantics we want.

**Step 2 — parse the flags with `getopt()`.**

```c
while ((opt = getopt(argc, argv, "lRx")) != -1) {
    switch (opt) {
    case 'l': mode = MODE_LONG;      break;
    case 'R': recursive = 1;         break;   /* orthogonal: not a mode */
    case 'x': mode = MODE_HORIZONTAL; break;   /* last of -l/-x wins */
    ...
    }
}
```
(lines 661–676)

Two deliberate design points:

* **`-l` and `-x` overwrite the same variable**, so the last one on the command
  line wins (`ls -lx` behaves like `ls -x`, `ls -xl` like `ls -l`) — matching GNU
  `ls`, where they are mutually exclusive output formats.
* **`-R` is *not* a mode.** Recursion composes with any output format
  (`ls -R`, `ls -lR`, `ls -xR` are all meaningful), so it is stored in its own
  `int recursive` and threaded through as a parameter rather than folded into
  `mode`.

**Step 3 — one dispatch point in `do_ls()`.**

```c
if (long_format) {                                   /* MODE_LONG */
    printf("total %ld\n", total_blocks);             /* first pass computed widths */
    for (...) print_long_entry(path, names[i], &w);
} else if (mode == MODE_HORIZONTAL) {                /* MODE_HORIZONTAL */
    print_horizontal(dir, names, count, max_len, get_terminal_width());
} else if (count > 0) {                              /* MODE_VERTICAL (default) */
    print_columnar(dir, names, count, max_len, get_terminal_width());
}
```
(lines 558–569)

with `int long_format = (mode == MODE_LONG);` hoisted at line 500 because the
long mode also controls two *earlier* phases — the width/block-count discovery
pass (lines 544–555) and the `total N` header. The `else if` chain makes the
priority explicit and guarantees exactly one printer runs.

**Step 4 — operand handling reuses the same mode.** `list_operand()` (lines
617–629) branches on `mode == MODE_LONG` to decide between a formatted
`print_long_entry()` and a plain `printf("%s\n", path)`, and `main()` skips the
shared-width computation entirely when `mode != MODE_LONG` (line 714).

In short: **parse → enum → pass it down → one `if/else if` chain picks the
printer.** Everything else (two passes, widths, terminal width, banners) is
conditionalised on that single variable, so adding a new mode means adding one
enum member, one `case`, and one `else if`.

---

## Feature-5: ls-v1.4.0 — Alphabetical Sort

### Q1. Why is it necessary to read all directory entries into memory before you can sort them? What are the potential drawbacks of this approach for directories containing millions of files?

**Why it is necessary.** Sorting is a *reordering* operation over a set, and
`readdir()` does not give you a set — it gives you a **cursor over a stream**:

* `readdir()` returns entries one at a time in whatever order the filesystem
  happens to store them (hash order for ext4, unsorted for many others). There is
  no API to ask "give me the 500th smallest name", and no API to rewind to a
  random position.
* You cannot compare entry *i* with entry *j* unless both are simultaneously
  alive. A comparison-based sort needs O(n log n) comparisons, each touching two
  entries; if the earlier one has been discarded, the comparison is impossible.
* You also cannot know how many entries there are until you have seen them all,
  so you cannot pre-size an output array or compute the layout (row/column
  geometry for the default mode) without a completed pass.

Hence `do_ls()` materialises everything first (lines 513–534): a growable array
of `strdup()`ed names, doubled via `realloc()` from an initial capacity of 64,
then `qsort(names, count, sizeof(char *), compare_names)` at line 541 — and only
after that does anything get printed.

**Drawbacks at scale (millions of files).**

1. **Memory.** Every name is copied with `strdup()`, so the resident cost is
   roughly `count × (strlen + 1)` bytes plus 8 bytes of pointer per entry plus
   `realloc` slack. One million names averaging 20 bytes ≈ 30 MB; ten million ≈
   300 MB. This is *file-name memory*, charged on top of whatever the kernel
   already buffers for the directory itself.
2. **`realloc` churn.** The doubling strategy keeps copies amortised O(n), but a
   large directory still causes several reallocations and copies as capacity
   grows 64 → 128 → … → 2ⁿ, with transient peak usage of ~1.5× the final array.
3. **Latency.** Nothing is printed until the whole directory has been read and
   sorted. On a slow network filesystem (NFS) or a directory with a million
   dentries not in cache, the user stares at an empty screen for the entire
   scan; a streaming implementation would have started printing immediately.
4. **Fragmentation / failure.** Many small `strdup` allocations fragment the
   heap, and any single allocation failure is fatal — our code `exit()`s
   (lines 521–530) because there is no useful partial result to show.
5. **Time.** O(n log n) comparisons where each `strcmp` may touch disk-cold name
   data; plus a second O(n) pass for `-l` widths (lines 545–554) and a third for
   `-R` (lines 582–589).
6. **Correctness pressure.** Sorting the *pointers* means the strings themselves
   must stay alive and unmodified for the whole sort — hence ownership lives in
   `names[]` and is released only after all printing is done (lines 592–594).

**How real tools cope:** a merge sort on runs that spill to disk, a bounded heap
plus external merge, or simply streaming in filesystem order and giving up on a
globally sorted output. GNU `ls` uses the same in-memory approach and relies on
the operating system's directory limits; the pathological cases are why tools
like `find` and `rsync` do not sort by default.

### Q2. Explain the purpose and signature of the comparison function required by `qsort()`. How does it work, and why must it take `const void *` arguments?

`qsort()` is **type-generic**: it sorts any array of fixed-size elements without
knowing what those elements are. Its declaration is:

```c
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));
```

* `base` — address of the array.
* `nmemb` — number of elements.
* `size` — byte size of **one** element (this is all `qsort` needs to do
  pointer arithmetic: element *i* lives at `base + i * size`).
* `compar` — your callback.

**Signature and contract of the comparator.** It must be
`int (*)(const void *, const void *)`, and it must return:

* a value **< 0** if the first element should sort before the second,
* **0** if they are equivalent,
* a value **> 0** if the first should sort after the second.

Only the *sign* matters; the magnitudes are meaningless (unlike `memcmp`'s byte
difference). The callback must also induce a **strict weak ordering** —
irreflexive and transitive — or the sort has undefined behaviour. It must be
**deterministic and side-effect free**: `qsort` may compare an element with
itself, may compare pairs in an unpredictable order (glibc uses merge sort/
introsort variants), and may therefore yield a different permutation for the same
input if the comparator is inconsistent. Because our comparator is plain
`strcmp`, the result is a stable-looking, repeatable ordering — and for equal
names the order does not matter anyway.

**Why `const void *`?**

1. **`void *` = "the element type is unknown."** `qsort` only knows an element is
   `size` bytes long; it cannot know whether that is a `char *`, an `int`, or a
   `struct operand`. So it passes back exactly the two addresses it has. Your
   callback is the only place that knows the real type, so *you* perform the cast.
2. **`const` = "the sort will not modify the array through this pointer."** The
   data must not change during ordering; only positions may change. It also
   prevents the callback from accidentally corrupting an element mid-sort (which
   would invalidate `qsort`'s internal bookkeeping) and documents intent at the
   call site. Note that `const` applies to the *pointer target*, not to what the
   pointer ultimately points at: a `const void *` argument can still be cast to
   `char **`.

**Our implementation** (lines 455–461):

```c
static int compare_names(const void *a, const void *b)
{
    const char *const *sa = a;   /* a is really a char **  */
    const char *const *sb = b;
    return strcmp(*sa, *sb);     /* dereference twice: void* -> char* -> char */
}
```

The array is `char **names`, so each *element* is a `char *`. `qsort` therefore
hands us addresses-of-elements, i.e. `char **`. The cast to `const char *const *`
makes both the outer pointer (to the element) and the inner pointer (to the
string) const; the first dereference yields a `const char *`, which `strcmp`
takes directly. Called as:

```c
qsort(names, count, sizeof(char *), compare_names);   /* line 541 */
```

`sizeof(char *)` is essential — it is the *element* size (8 bytes on x86-64), not
the size of a string and not the size of the array. The same pattern is reused
for the command-line operand array in `compare_operands()` (lines 646–652), which
sorts `struct operand` elements with `sizeof(*ops)`.

---

## Feature-6: ls-v1.5.0 — Colorized Output

### Q1. How do ANSI escape codes work to produce color in a standard Linux terminal? Show the specific code sequence for printing text in green.

An **ANSI escape sequence** is an out-of-band control string embedded in the
character stream. It starts with `ESC` (byte `0x1B`), and the terminal driver /
terminal emulator intercepts it instead of drawing it. The general form used for
colour is a **Control Sequence Introducer**:

```
ESC [ <parameters> M
│ │   │            └─ final byte: 'm' = SGR (Select Graphic Rendition)
│ │   └─ parameters separated by ';'
│ └─ CSI: '[' (0x5B)
└─ ESC:  0x1B
```

So the byte sequence for "set foreground to green" is `0x1B 0x5B 0x33 0x32 0x6D`,
written in C as `"\033[32m"`. The terminal switches its current rendition for all
*subsequently* written characters; ordinary text keeps flowing through stdout
unchanged. Nothing about the text data itself is modified — colour is a property
of the stream position and the terminal's state machine.

**The specific sequence for green:**

```c
printf("\033[32m");     /* green            */
printf("hello");
printf("\033[0m");      /* reset to default */
```

`\033` and `\e` are both C escapes for `0x1B`. Parameters follow the SGR table:

| code | meaning |
|---|---|
| `0`  | **reset all attributes** (must always be emitted to stop the colour) |
| `1`  | bold |
| `7`  | reverse video |
| `30`–`37` | foreground: black, red, **green**, yellow, blue, magenta, cyan, white |
| `40`–`47` | background of the same eight colours |
| `90`–`97` | bright variants |

**In this program** the codes are macros (lines 171–176):

```c
#define COLOR_RESET   "\033[0m"     /* always turn colour back off */
#define COLOR_DIR     "\033[0;34m"  /* blue   */
#define COLOR_EXEC    "\033[0;32m"  /* green  */
#define COLOR_ARCHIVE "\033[0;31m"  /* red    */
#define COLOR_LINK    "\033[0;35m"  /* magenta*/
#define COLOR_SPECIAL "\033[7m"     /* reverse video */
```

and `print_colored()` (lines 212–227) does the sandwich:

```c
const char *color = color_for(name, mode);
if (color) fputs(color, stdout);
fputs(name, stdout);
fputs(COLOR_RESET, stdout);   /* ALWAYS, even if color was NULL */
```

The leading `0;` in `"\033[0;32m"` is deliberate: it resets everything first and
then applies green, so a colour can never inherit a stray bold/reverse attribute
left by a previous entry. Because a reset is written unconditionally after every
name, an entry with no colour still restores the terminal — which is what stops
the *rest of the shell prompt* from turning blue after `ls` exits.

### Q2. To color an executable file, you need to check its permission bits. Explain which bits in the `st_mode` field you need to check to determine if a file is executable by the owner, group, or others.

The nine permission bits of `st_mode` are laid out as three triplets, each
low-bit-first inside its group; the C macros name them:

| who | read | write | **execute** | octal mask |
|---|---|---|---|---|
| owner (user) | `S_IRUSR` `0400` | `S_IWUSR` `0200` | **`S_IXUSR` `0100`** | `0700` |
| group        | `S_IRGRP` `0040` | `S_IWGRP` `0020` | **`S_IXGRP` `0010`** | `0070` |
| others       | `S_IROTH` `0004` | `S_IWOTH` `0002` | **`S_IXOTH` `0001`** | `0007` |

Each is a single-bit constant, so `&` isolates it:

```c
if (st.st_mode & S_IXUSR)   /* executable by owner   */ ...
if (st.st_mode & S_IXGRP)   /* executable by group   */ ...
if (st.st_mode & S_IXOTH)   /* executable by others  */ ...
```

For "executable by *anyone*" the three are OR-ed — which is exactly what a mask
of the combined bits does in one test:

```c
if ((mode & 0111) != 0)          /* line 204: 0111 = S_IXUSR|S_IXGRP|S_IXOTH */
    return COLOR_EXEC;           /* green */
```

`color_for()` (lines 196–208) consults the bits in priority order: symlink →
directory → special file → **any execute bit** → archive → default (no colour).
Note that only the *permission* bits are tested; the file type (`S_IFMT`) has
already been handled by the earlier `S_ISLNK`/`S_ISDIR`/`S_ISCHR`-style checks,
and the setuid/setgid/sticky bits (`S_ISUID 04000`, `S_ISGID 02000`,
`S_ISVTX 01000`) live in the *eleventh* bit and are deliberately **not** included
in `0111` — a setuid-but-not-executable file is not executable.

Two practical subtleties worth knowing for the viva:

* **`root` does not bypass this.** Unlike most permission checks, the kernel
  refuses to `execve()` a file that has *no* execute bit set for anybody — even
  for uid 0. So `0111` is the right and only test.
* **Script vs binary.** Execute permission is necessary and (for the kernel)
  sufficient to start the file; the `#!` line decides what actually interprets a
  script. `ls` only ever looks at the mode bits, so the colour reflects the mode,
  not the interpreter.
* **Type can override permissions:** directories are almost always `rwx`, but
  they are coloured blue because `S_ISDIR` is checked first — otherwise every
  directory would wrongly appear green.

`mode_to_string()` (lines 85–96) shows the finer-grained version of the same
tests, where the execute bit combined with setuid/setgid/sticky renders `s`, `S`,
`t` or `T` instead of `x`.

---

## Feature-7: ls-v1.6.0 — Recursive Listing (`-R`)

### Q1. In a recursive function, what is a "base case"? In the context of your recursive `ls`, what is the base case that stops the recursion from continuing forever?

A **base case** is the condition under which the function returns *without* making
another recursive call. It is the bottom of the call stack; without it, the
function would call itself indefinitely and eventually exhaust the stack
(`SIGSEGV`) — and, for a directory walk, it is also what prevents the program from
looping forever over the data structure itself.

In `do_ls()` the recursion is guarded in three layers:

1. **Structural base case — no subdirectories.** The descent is a loop, not an
   unconditional call (lines 581–590):

   ```c
   if (recursive) {
       for (i = 0; i < count; i++) {
           char *path = build_path(dir, names[i]);
           struct stat st;
           if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode))
               do_ls(path, mode, 1, 1);
           free(path);
       }
   }
   ```

   A recursive call happens **only for entries that are directories**. A leaf
   directory (or a directory containing only files) yields zero recursive calls,
   so the unwinding begins there. This is the analogue of `if (n <= 0) return;` in
   a numeric recursion.

2. **Cycle base case — `.` and `..` are never entered.** Because
   `names[]` is populated with the `entry->d_name[0] == '.'` filter (line 514), the
   current directory and its parent are *never* in the array. Without this filter
   the very first recursion would go `a → a/. → a/./. → …` and never terminate,
   even though condition 1 held. So the filter is what makes the filesystem tree
   a genuine *tree* rather than a graph with back-edges.

3. **Error base case — `opendir()` failure.** `do_ls()` returns immediately if
   `opendir()` fails (lines 502–506), printing a diagnostic and not descending
   further. This is the "can't go deeper, stop" clause (permission denied,
   vanished directory, non-directory passed in).

Two properties guarantee termination in practice:

* **Depth is finite.** Real directory trees are finite and acyclic once `.`/`..`
  are excluded, so the recursion depth is bounded by the deepest path in the
  tree.
* **Symlinks are not followed.** The test uses `lstat()`, not `stat()` (line
  586), so a symlink pointing at a directory is *not* treated as a directory to
  descend into. Had we used `stat()`, a link `loop -> .` would make the walk chase
  its own tail forever — condition 1 would be satisfied at every step and the
  stack would grow without bound.

So: the base case is **"this directory has no subdirectory to descend into (or we
cannot open it)"**, and the two supporting decisions (filter dot entries, use
`lstat`) are what keep that base case reachable.

### Q2. Explain why it is essential to construct a full path (e.g., `parent_dir/subdir`) before making a recursive call. What would happen if you simply called `do_ls("subdir")` from within the `do_ls("parent_dir")` function call?

**Why the full path is required.** Path resolution in POSIX is always relative to
the process's **current working directory** (`cwd`), *never* relative to the
directory that happens to be open on some `DIR *`. `opendir("subdir")` therefore
asks the kernel for `<cwd>/subdir`. Inside `do_ls("parent_dir")` the program has
no idea what `parent_dir`'s contents look like to the kernel — it only has names
read out of the directory stream. To open a child, you must hand the kernel the
path that resolves to that child, which means prefixing the parent's path:

```c
char *path = build_path(dir, names[i]);   /* "parent_dir/subdir" */
...
if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode))
    do_ls(path, mode, 1, 1);              /* recurse with the FULL path */
```
(lines 583–587)

`build_path()` (line 57) also takes care of inserting the `/` exactly once —
including *not* doubling it when the operand already ends in `/`, so
`ls -R nested/` prints the banner `nested/deep:` and not `nested//deep:`.

**What goes wrong with `do_ls("subdir")`.**

1. **It resolves against `cwd`, not against `parent_dir`.** The recursive call
   would try to open `<cwd>/subdir`. If that path does not exist — which it
   normally doesn't, because `subdir` is nested inside `parent_dir` — `opendir()`
   fails with `ENOENT`, `do_ls` prints `ls: cannot open directory 'subdir': No
   such file or directory` and returns. The result: the top-level directory is
   listed, every section header claims there is more to come, and **all
   subdirectories come out empty or missing**.
2. **Silent wrong-data output is even worse than an error.** If a *different*
   `subdir` happens to exist directly under `cwd`, the recursion happily lists
   **that** directory instead — correct-looking output describing the wrong tree.
   Nothing in the program detects the mistake.
3. **It breaks the banners.** The header is `printf("%s:\n", dir)` (line 479), so
   the section titles would read `subdir:`, `deep:`, `nested:` — bare leaf names
   instead of the meaningful `parent_dir/subdir:` that tells the user where they
   are. The whole point of `-R` output layout is to show the path of each section.
4. **It would require changing `cwd`.** The only way to make relative names work
   would be `chdir()` into each directory and pass bare names. That introduces
   hidden global state: every other relative path in the program (operands,
   error messages, `build_path` results) would silently change meaning, `chdir`
   failure handling would be needed everywhere, and the program could no longer
   be re-entered safely. It also breaks with multiple operands, since `ls a b`
   must be resolvable from the original `cwd`.
5. **Symlink/relative-path edge cases.** Passing a bare name loses the parent
   context entirely, so `lstat()` (line 586) would be testing the wrong entry —
   the type check that decides whether to recurse would be made against an
   unrelated path.

In short: `readdir()` gives you a *name*, the kernel resolves *paths*, and the
only bridge between them is a correctly joined `parent + "/" + name`. Each
recursive invocation must therefore carry its own absolute-with-respect-to-the-
operand path, which is exactly what `build_path()` + `do_ls(path, ...)` do — and
that is also why every entry printed inside a recursive section is `lstat()`ed
and printed relative to the same `dir`.

---

## Summary of concepts covered

| Feature | Version | Concepts | Key code |
|---|---|---|---|
| 1 | setup | Git/GitHub, scaffolding, Makefile | repo, `Makefile` |
| 2 | v1.1.0 | `stat`/`lstat`, `st_mode`, bitwise operators, `getpwuid`, `getgrgid` | `mode_to_string`, `print_long_entry` |
| 3 | v1.2.0 | column-major layout, `ioctl(TIOCGWINSZ)`, two-pass width calculation | `print_columnar`, `layout_fits` |
| 4 | v1.3.0 | row-major layout, greedy wrapping, `getopt`, mode dispatch | `print_horizontal` |
| 5 | v1.4.0 | dynamic arrays, `qsort`, generic comparator, `const void *` | `compare_names` |
| 6 | v1.5.0 | ANSI SGR escape codes, permission-bit masks | `color_for`, `print_colored` |
| 7 | v1.6.0 | recursion, base case, path construction, `lstat` vs `stat` | `do_ls`, `build_path`, `print_dir_header` |
