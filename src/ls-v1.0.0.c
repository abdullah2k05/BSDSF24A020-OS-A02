/*
 * Programming Assignment 02: Re-engineering the ls Utility
 * File: src/ls-v1.0.0.c  -- the base v1.0.0 source, extended in place.
 *
 * v1.0.0 : simple directory listing (opendir/readdir)
 * v1.1.0 : adds the -l (long listing) option
 * v1.2.0 : default output is a multi-column (down then across) listing
 * v1.3.0 : adds the -x (horizontal / left-to-right) option
 * v1.4.0 : directory entries are sorted alphabetically with qsort()
 *
 * Usage:
 *       $ ./bin/ls
 *       $ ./bin/ls -l
 *       $ ./bin/ls -x
 *       $ ./bin/ls -l src
 *       $ ./bin/ls -lx src
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

extern int errno;

/*
 * v1.3.0 -- display-mode state.
 * One variable decides which printer runs; -l always wins over -x,
 * which matches GNU ls.
 */
enum display_mode {
    MODE_VERTICAL = 0,   /* default: columns, down then across */
    MODE_LONG,           /* -l */
    MODE_HORIZONTAL      /* -x */
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/* Join a directory path and an entry name into "dir/name". */
static char *build_path(const char *dir, const char *name)
{
    size_t len = strlen(dir) + strlen(name) + 2;
    char *path = malloc(len);
    if (path == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    snprintf(path, len, "%s/%s", dir, name);
    return path;
}

/* Turn the type bits + permission bits of st_mode into "drwxr-xr-x". */
static void mode_to_string(mode_t mode, char *str)
{
    if (S_ISDIR(mode))       str[0] = 'd';
    else if (S_ISLNK(mode))  str[0] = 'l';
    else if (S_ISCHR(mode))  str[0] = 'c';
    else if (S_ISBLK(mode))  str[0] = 'b';
    else if (S_ISFIFO(mode)) str[0] = 'p';
    else if (S_ISSOCK(mode)) str[0] = 's';
    else                     str[0] = '-';

    str[1] = (mode & S_IRUSR) ? 'r' : '-';
    str[2] = (mode & S_IWUSR) ? 'w' : '-';
    str[3] = (mode & S_ISUID) ? ((mode & S_IXUSR) ? 's' : 'S')
                              : ((mode & S_IXUSR) ? 'x' : '-');

    str[4] = (mode & S_IRGRP) ? 'r' : '-';
    str[5] = (mode & S_IWGRP) ? 'w' : '-';
    str[6] = (mode & S_ISGID) ? ((mode & S_IXGRP) ? 's' : 'S')
                              : ((mode & S_IXGRP) ? 'x' : '-');

    str[7] = (mode & S_IROTH) ? 'r' : '-';
    str[8] = (mode & S_IWOTH) ? 'w' : '-';
    str[9] = (mode & S_ISVTX) ? ((mode & S_IXOTH) ? 't' : 'T')
                              : ((mode & S_IXOTH) ? 'x' : '-');
    str[10] = '\0';
}

/*
 * Format st_mtime the way GNU ls does, using ctime() as the source string.
 *   recent files (< ~6 months) : "Oct 12 20:30"
 *   older files                : "Oct 12  2024"
 */
static void format_mtime(time_t t, char *out, size_t n)
{
    char *ct = ctime(&t);   /* "Www Mmm dd hh:mm:ss yyyy" -- always 24 chars */

    if (ct == NULL) {
        snprintf(out, n, "??? ??? ?? ??:??:?? ????");
        return;
    }

    time_t now = time(NULL);
    double age = difftime(now, t);

    if (age >= 0 && age < 15768000)          /* 180 days */
        snprintf(out, n, "%.3s %.2s %.5s", ct + 4, ct + 8, ct + 11);
    else
        snprintf(out, n, "%.3s %.2s  %.4s", ct + 4, ct + 8, ct + 20);
}

/* Column widths so every line of a directory listing lines up. */
struct widths {
    int link;
    int user;
    int group;
    int size;
};

/* Derive the column widths of a single entry. */
static void widths_from_stat(const struct stat *st, struct widths *w)
{
    int n;
    struct passwd *pw;
    struct group *gr;

    n = snprintf(NULL, 0, "%ld", (long)st->st_nlink);
    if (n > w->link) w->link = n;

    n = snprintf(NULL, 0, "%ld", (long)st->st_size);
    if (n > w->size) w->size = n;

    pw = getpwuid(st->st_uid);
    if (pw != NULL && (int)strlen(pw->pw_name) > w->user)
        w->user = (int)strlen(pw->pw_name);

    gr = getgrgid(st->st_gid);
    if (gr != NULL && (int)strlen(gr->gr_name) > w->group)
        w->group = (int)strlen(gr->gr_name);
}

/* Print one line of an "ls -l" listing. */
static void print_long_entry(const char *path, const char *name,
                             const struct widths *w)
{
    struct stat st;
    struct passwd *pw;
    struct group *gr;
    const char *user, *group;
    char mode[11];
    char tbuf[32];

    if (lstat(path, &st) == -1) {
        fprintf(stderr, "ls: cannot access '%s': %s\n", path, strerror(errno));
        return;
    }

    mode_to_string(st.st_mode, mode);

    pw = getpwuid(st.st_uid);
    user = (pw != NULL) ? pw->pw_name : "?";
    gr = getgrgid(st.st_gid);
    group = (gr != NULL) ? gr->gr_name : "?";

    format_mtime(st.st_mtime, tbuf, sizeof(tbuf));

    printf("%s %*ld %-*s %-*s %*ld %s %s",
           mode,
           w->link, (long)st.st_nlink,
           w->user, user,
           w->group, group,
           w->size, (long)st.st_size,
           tbuf, name);

    if (S_ISLNK(st.st_mode)) {
        char target[PATH_MAX + 1];
        ssize_t n = readlink(path, target, sizeof(target) - 1);
        if (n >= 0) {
            target[n] = '\0';
            printf(" -> %s", target);
        }
    }
    putchar('\n');
}

/* ------------------------------------------------------------------ */
/* v1.2.0: column display (down then across)                           */
/* ------------------------------------------------------------------ */

/* Width of the terminal attached to stdout; 80 when it cannot be read. */
static int get_terminal_width(void)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return (int)ws.ws_col;
    return 80;
}

/* Width of column `col` in a layout that has `num_rows` rows. */
static size_t column_span(size_t col, size_t num_rows,
                          char **names, size_t count)
{
    size_t row, best = 0;

    for (row = 0; row < num_rows; row++) {
        size_t idx = col * num_rows + row;
        size_t len;

        if (idx >= count)
            break;
        len = strlen(names[idx]);
        if (len > best)
            best = len;
    }
    return best;
}

/* Would a layout with `num_rows` rows still fit inside `term_width`? */
static int layout_fits(size_t num_rows, char **names, size_t count,
                       int term_width)
{
    size_t num_cols = (count + num_rows - 1) / num_rows;
    size_t total = 0, col;

    for (col = 0; col < num_cols; col++)
        total += column_span(col, num_rows, names, count) + 2;

    total -= 2;                       /* last column needs no trailing pad */
    return total <= (size_t)term_width;
}

/*
 * v1.2.0 -- default output: columns filled DOWN, then ACROSS.
 *
 * Starting point is the rule from the assignment:
 *     num_cols = terminal_width / (longest_name + spacing)
 *     num_rows = ceil(count / num_cols)
 * Because real column widths depend on which names land in which column,
 * we then try to reduce the row count (i.e. gain columns) for as long as
 * the layout still fits the terminal -- this is what GNU ls ends up doing.
 *
 * Item i is printed at row = i % num_rows, col = i / num_rows.
 */
static void print_columnar(char **names, size_t count, size_t max_len,
                           int term_width)
{
    size_t max_cols, num_rows, num_cols, row, col;

    if (count == 0)
        return;

    max_cols = (size_t)term_width / (max_len + 2);   /* +2 = column spacing */
    if (max_cols < 1)
        max_cols = 1;
    if (max_cols > count)
        max_cols = count;

    num_rows = (count + max_cols - 1) / max_cols;    /* ceiling division */
    if (num_rows < 1)
        num_rows = 1;

    while (num_rows > 1 &&
           layout_fits(num_rows - 1, names, count, term_width))
        num_rows--;

    num_cols = (count + num_rows - 1) / num_rows;

    for (row = 0; row < num_rows; row++) {
        /* right-most column that actually has an item in this row */
        size_t last_col = 0;
        for (col = 0; col < num_cols; col++)
            if (col * num_rows + row < count)
                last_col = col;

        for (col = 0; col < num_cols; col++) {
            size_t idx = col * num_rows + row;
            size_t len;

            if (idx >= count)
                continue;

            len = strlen(names[idx]);
            fputs(names[idx], stdout);
            if (col != last_col)
                printf("%*s",
                       (int)(column_span(col, num_rows, names, count) + 2 - len),
                       "");
        }
        putchar('\n');
    }
}

/* ------------------------------------------------------------------ */
/* v1.3.0: horizontal column display (-x)                              */
/* ------------------------------------------------------------------ */

/*
 * Row-major fill: print left to right and only wrap when the next name
 * would run past the right edge of the terminal.  Columns are uniform,
 * wide enough for the longest filename plus 2 spaces of spacing.
 */
static void print_horizontal(char **names, size_t count, size_t max_len,
                             int term_width)
{
    size_t col_width = max_len + 2;
    size_t pos = 0;                      /* cursor column on this line */
    size_t i;

    if (count == 0)
        return;

    for (i = 0; i < count; i++) {
        size_t len = strlen(names[i]);

        /* wrap before printing, but never emit a leading blank line */
        if (pos > 0 && pos + len > (size_t)term_width) {
            putchar('\n');
            pos = 0;
        }

        fputs(names[i], stdout);
        pos += len;

        if (i + 1 < count) {
            size_t next_col  = pos + (col_width - len);
            size_t next_len  = strlen(names[i + 1]);

            if (next_col <= (size_t)term_width &&
                next_col + next_len <= (size_t)term_width) {
                printf("%*s", (int)(col_width - len), "");
                pos = next_col;
            } else {
                /* no room left for another column: end the line here */
                pos = (size_t)term_width + 1;
            }
        }
    }
    putchar('\n');
}

/* ------------------------------------------------------------------ */
/* v1.4.0: alphabetical sort                                           */
/* ------------------------------------------------------------------ */

/*
 * Comparison routine handed to qsort().  It receives pointers to two
 * elements of the array -- and since the array holds char *, each
 * element is itself a char *, so the arguments are char **.
 * Casting away the void and dereferencing once yields the two strings.
 */
static int compare_names(const void *a, const void *b)
{
    const char *const *sa = a;
    const char *const *sb = b;

    return strcmp(*sa, *sb);
}

/* ------------------------------------------------------------------ */
/* core directory listing                                              */
/* ------------------------------------------------------------------ */

void do_ls(const char *dir, enum display_mode mode)
{
    struct dirent *entry;
    DIR *dp = opendir(dir);
    char **names = NULL;
    size_t count = 0, cap = 0, i;
    size_t max_len = 0;
    struct widths w = {1, 1, 1, 1};
    long total_blocks = 0;
    int long_format = (mode == MODE_LONG);

    if (dp == NULL) {
        fprintf(stderr, "ls: cannot open directory '%s': %s\n",
                dir, strerror(errno));
        return;
    }

    /* --- read every (non-hidden) entry into a dynamic array of strings --- */
    errno = 0;
    while ((entry = readdir(dp)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        if (count == cap) {
            cap = (cap == 0) ? 64 : cap * 2;
            char **tmp = realloc(names, cap * sizeof(char *));
            if (tmp == NULL) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
            names = tmp;
        }
        names[count] = strdup(entry->d_name);
        if (names[count] == NULL) {
            perror("strdup");
            exit(EXIT_FAILURE);
        }
        if (strlen(names[count]) > max_len)
            max_len = strlen(names[count]);
        count++;
    }
    if (errno != 0)
        perror("readdir failed");
    closedir(dp);

    /* --- sort alphabetically before anything is printed (v1.4.0) --- */
    if (count > 1)
        qsort(names, count, sizeof(char *), compare_names);

    /* --- first pass over the names: alignment widths + total blocks --- */
    if (long_format) {
        for (i = 0; i < count; i++) {
            char *path = build_path(dir, names[i]);
            struct stat st;
            if (lstat(path, &st) == 0) {
                widths_from_stat(&st, &w);
                /* st_blocks counts 512-byte units; ls reports 1024-byte units */
                total_blocks += st.st_blocks / 2;
            }
            free(path);
        }
    }

    /* --- second pass: actually print --- */
    if (long_format) {
        printf("total %ld\n", total_blocks);
        for (i = 0; i < count; i++) {
            char *path = build_path(dir, names[i]);
            print_long_entry(path, names[i], &w);
            free(path);
        }
    } else if (mode == MODE_HORIZONTAL) {
        print_horizontal(names, count, max_len, get_terminal_width());
    } else if (count > 0) {
        print_columnar(names, count, max_len, get_terminal_width());
    }

    for (i = 0; i < count; i++)
        free(names[i]);
    free(names);
}

/* List a single command-line operand (file or directory). */
static void list_operand(const char *path, enum display_mode mode)
{
    struct stat st;

    if (stat(path, &st) == -1) {
        fprintf(stderr, "ls: cannot access '%s': %s\n", path, strerror(errno));
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        do_ls(path, mode);
    } else if (mode == MODE_LONG) {
        struct widths w = {1, 1, 1, 1};
        widths_from_stat(&st, &w);
        print_long_entry(path, path, &w);
    } else {
        printf("%s\n", path);
    }
}

int main(int argc, char *argv[])
{
    int opt;
    enum display_mode mode = MODE_VERTICAL;
    int noper;

    while ((opt = getopt(argc, argv, "lx")) != -1) {
        switch (opt) {
        case 'l':
            mode = MODE_LONG;
            break;
        case 'x':
            if (mode != MODE_LONG)          /* -l takes precedence, like GNU ls */
                mode = MODE_HORIZONTAL;
            break;
        default:
            fprintf(stderr, "Usage: %s [-l] [-x] [file ...]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    noper = argc - optind;

    if (noper == 0) {
        list_operand(".", mode);
        return EXIT_SUCCESS;
    }

    for (int i = optind; i < argc; i++) {
        struct stat st;
        int is_dir = (stat(argv[i], &st) == 0) && S_ISDIR(st.st_mode);

        /* GNU ls only prints a "dir:" header when several operands exist. */
        if (noper > 1 && is_dir) {
            if (i > optind)
                printf("\n");
            printf("%s:\n", argv[i]);
        }
        list_operand(argv[i], mode);
    }

    return EXIT_SUCCESS;
}
