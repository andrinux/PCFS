#define FUSE_USE_VERSION 26
 
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fuse.h>
 
#define BUFFSIZE 8192
 
static int content_size;
static char content[BUFFSIZE];
static const char* fname = "hello-world";
 
static int ou_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info* fi)
{
    return filler(buf, fname, NULL, 0);
}
 
static int ou_getattr(const char* path, struct stat* st)
{
    memset(st, 0, sizeof(struct stat));
 
    if (strcmp(path, "/") == 0) {
        st->st_mode = 0755 | S_IFDIR;
        st->st_size = strlen(fname);
    } else {
        st->st_mode = 0644 | S_IFREG;
        st->st_size = content_size;
    }
 
    return 0;
}
 
static int ou_read(const char* path, char* buf, size_t bytes, off_t offset,
                   struct fuse_file_info* fi)
{
    size_t available;
 
    if (strcmp(path + 1, fname) != 0)
        return -ENOENT;
 
    if (offset >= content_size)
        return 0;
 
    available = content_size - offset;
    if (available < bytes)
        bytes = available;
 
    memcpy(buf, content + offset, bytes);
 
    return bytes;
}
 
static int ou_write(const char* path, const char* buf, size_t bytes,
                    off_t offset, struct fuse_file_info* fi)
{
    size_t new_size;
 
    if (strcmp(path + 1, fname) != 0)
        return -ENOENT;
 
    new_size = offset + bytes;
    if (new_size > BUFFSIZE)
        return -EFBIG;
 
    memcpy(content + offset, buf, bytes);
 
    if (content_size < new_size)
        content_size = new_size;
 
    return bytes;
}
 
static int ou_truncate(const char* path, off_t size)
{
    if (size > BUFFSIZE)
        return -EFBIG;
 
    if (content_size < size)
        memset(content + content_size, 0, size - content_size);
 
    content_size = size;
 
    return 0;
}
 
static struct fuse_operations oufs_ops = {
    .readdir    =   ou_readdir,
    .getattr    =   ou_getattr,
    .read       =   ou_read,
    .write      =   ou_write,
    .truncate   =   ou_truncate,
};
 
int main(int argc, char* argv[])
{
    return fuse_main(argc, argv, &oufs_ops, NULL);
}