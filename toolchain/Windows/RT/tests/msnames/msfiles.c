/* msfiles.c - Microsoft's descriptor calls and their old spellings, as the UCRT answers them (docs\Runtime_Design.md
   9.4): _open, _close, _read, _write, _lseek, _dup, _dup2, _isatty, _unlink, _fdopen, _fstat. errno is told by its
   name, the C6747's numbers being its own; the standard descriptors are not asked about (a probe's are redirected on
   Windows, and the console under sim6747). */
#define _POSIX_SOURCE 1
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

static const char *why(void)
{
    return errno == ENOENT ? "ENOENT" : errno == EEXIST ? "EEXIST" : errno == EBADF ? "EBADF"
         : errno == EINVAL ? "EINVAL" : "another";
}

static void bytes(const char *what, const char *b, int n)
{
    int i;
    printf("%s (%d):", what, n);
    for (i = 0; i < n; i++) {
        if (b[i] == '\n') printf(" \\n");
        else if (b[i] == '\r') printf(" \\r");
        else printf(" %c", b[i]);
    }
    printf("\n");
}

static void stat_of(const char *what, int fd)
{
    struct stat st;
    int r = fstat(fd, &st);
    printf("%s: %d %s, size %ld, links %d, permissions %o\n", what, r,
           (st.st_mode & _S_IFMT) == _S_IFREG ? "a file" : "not a file", (long)st.st_size, st.st_nlink,
           (unsigned)(st.st_mode & 0777));
}

int main(void)
{
    const char *name = "msfiles1.tmp", *other = "msfiles2.tmp";
    char buf[64];
    int fd, fd2, n;
    FILE *f;

    _unlink(name);
    _unlink(other);
    fd = _open(name, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
    printf("_open a new file: %s\n", fd >= 3 ? "a descriptor" : "refused");
    printf("_write: %d\n", _write(fd, "hello\nworld\n", 12));
    printf("_isatty of a file: %d\n", _isatty(fd));
    stat_of("_fstat", fd);
    printf("_close: %d\n", _close(fd));

    fd = _open(name, _O_RDONLY | _O_BINARY);
    n = _read(fd, buf, sizeof buf);
    bytes("_read", buf, n);
    printf("_read at the end: %d\n", _read(fd, buf, sizeof buf));
    printf("_lseek to 6: %ld\n", _lseek(fd, 6, SEEK_SET));
    n = _read(fd, buf, 5);
    bytes("_read 5", buf, n);
    printf("_lseek: here %ld,", _lseek(fd, 0, SEEK_CUR));
    printf(" the end %ld,", _lseek(fd, 0, SEEK_END));
    printf(" back from it %ld\n", _lseek(fd, -3, SEEK_END));

    fd2 = _dup(fd);
    printf("_dup: %s\n", fd2 >= 3 && fd2 != fd ? "another descriptor" : "refused");
    printf("  where it is: %ld\n", _lseek(fd2, 0, SEEK_CUR));
    printf("  moved through it: %ld\n", _lseek(fd2, 0, SEEK_SET));
    n = _read(fd, buf, 5);
    bytes("  the first one reads from there", buf, n);
    printf("_dup2 to 20: %d\n", _dup2(fd, 20));
    n = _read(20, buf, 3);
    bytes("  20 reads on", buf, n);
    printf("_close the first: %d\n", _close(fd));
    n = _read(fd2, buf, 4);
    bytes("  the second still reads", buf, n);
    printf("_close the others: %d %d\n", _close(fd2), _close(20));

    fd = _open(name, _O_WRONLY | _O_TRUNC | _O_TEXT);
    printf("_write in text mode: %d\n", _write(fd, "a\nb\n", 4));
    _close(fd);
    fd = _open(name, _O_RDONLY | _O_BINARY);
    stat_of("  _fstat", fd);
    n = _read(fd, buf, sizeof buf);
    bytes("  the bytes", buf, n);
    _close(fd);
    fd = _open(name, _O_RDONLY | _O_TEXT);
    n = _read(fd, buf, sizeof buf);
    bytes("  read in text mode", buf, n);
    _close(fd);

    fd = _open(name, _O_RDONLY | _O_TEXT);
    f = _fdopen(fd, "r");
    printf("_fdopen: %s\n", f ? "a stream" : "null");
    while (f && fgets(buf, sizeof buf, f)) bytes("  fgets", buf, (int)strlen(buf));
    printf("  fclose: %d\n", f ? fclose(f) : -9);
    fd = _open(other, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
    f = _fdopen(fd, "wb");
    fprintf(f, "%d-%s\n", 42, "fdopen");
    fclose(f);
    fd = _open(other, _O_RDONLY | _O_BINARY);
    n = _read(fd, buf, sizeof buf);
    bytes("  written through it", buf, n);
    _close(fd);

    errno = 0;
    n = _open(name, _O_WRONLY | _O_CREAT | _O_EXCL, _S_IREAD | _S_IWRITE);
    printf("_open _O_EXCL of a file that is there: %d %s\n", n, why());
    errno = 0;
    n = _open("msfiles-none.tmp", _O_RDONLY);
    printf("_open of a file that is not: %d %s\n", n, why());

    fd = open(other, O_RDONLY | O_BINARY);
    fd2 = dup(fd);
    printf("open, dup: %d,", fd >= 3 && fd2 >= 3);
    printf(" lseek %ld,", lseek(fd2, 3, SEEK_SET));
    printf(" read %d,", read(fd, buf, 2));
    printf(" isatty %d,", isatty(fd));
    printf(" dup2 %d\n", dup2(fd2, 21));
    f = fdopen(21, "rb");
    printf("fdopen: %c\n", f ? fgetc(f) : '?');
    printf("close: %d %d %d\n", close(fd), close(fd2), f ? fclose(f) : -9);
    {
        int w = open(other, O_WRONLY | O_APPEND);
        printf("write appending: %d\n", write(w, "+", 1));
        close(w);
    }

    printf("_unlink: %d %d\n", _unlink(name), unlink(other));
    errno = 0;
    printf("_unlink again: %d %s\n", _unlink(name), why());
    return 0;
}
