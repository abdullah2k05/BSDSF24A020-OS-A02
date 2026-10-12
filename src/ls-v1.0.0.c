/*
 * Programming Assignment 02: Re-engineering the ls Utility
 * File: src/ls-v1.0.0.c  -- the base v1.0.0 source, extended in place.
 *
 * v1.0.0 : simple directory listing (opendir/readdir)
 * v1.1.0 : adds the -l (long listing) option
 *
 * Usage:
 *       $ ./bin/ls
 *       $ ./bin/ls -l
 *       $ ./bin/ls -l src
 *       $ ./bin/ls -l src Makefile
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

extern int errno;

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
/* core directory listing                                              */
/* ------------------------------------------------------------------ */

void do_ls(const char *dir, int long_format)
{
    struct dirent *entry;
    DIR *dp = opendir(dir);
    char **names = NULL;
    size_t count = 0, cap = 0, i;
    struct widths w = {1, 1, 1, 1};
    long total_blocks = 0;

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
        count++;
    }
    if (errno != 0)
        perror("readdir failed");
    closedir(dp);

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
    } else {
        for (i = 0; i < count; i++)
            printf("%s\n", names[i]);
    }

    for (i = 0; i < count; i++)
        free(names[i]);
    free(names);
}

/* List a single command-line operand (file or directory). */
static void list_operand(const char *path, int long_format)
{
    struct stat st;

    if (stat(path, &st) == -1) {
        fprintf(stderr, "ls: cannot access '%s': %s\n", path, strerror(errno));
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        do_ls(path, long_format);
    } else if (long_format) {
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
    int long_format = 0;
    int noper;

    while ((opt = getopt(argc, argv, "l")) != -1) {
        switch (opt) {
        case 'l':
            long_format = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-l] [file ...]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    noper = argc - optind;

    if (noper == 0) {
        list_operand(".", long_format);
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
        list_operand(argv[i], long_format);
    }

    return EXIT_SUCCESS;
}
