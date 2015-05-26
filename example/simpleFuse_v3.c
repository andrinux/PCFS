//gcc -Wall simpleFuse_v3.c `pkg-config fuse --cflags --libs` -o hello
//Support functions to create and delete files

#define FUSE_USE_VERSION 26
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fuse.h>
 
#include "list.h"
 
#define MAX_NAMELEN 255

//This is the denotation of a file ertry
//includes a mode, a linked list position, a name
/*
    struct list_node {
        struct list_node *prev, *next;
    };


#define list_for_each(p, head) \
    for ((p) = (head)->next; (p) != (head); (p) = (p)->next)

*/
struct ou_entry {
    mode_t mode;
    struct list_node node;
    char name[MAX_NAMELEN + 1];
};
 
static struct list_node entries;
 
static int ou_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info* fi)
{
    struct list_node* n;
 
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
 
    list_for_each (n, &entries) {
        struct ou_entry* o = list_entry(n, struct ou_entry, node);
        filler(buf, o->name, NULL, 0);
    }
 
    return 0;
}
 
static int ou_getattr(const char* path, struct stat* st)
{
    struct list_node* n;
 
    memset(st, 0, sizeof(struct stat));
 
    if (strcmp(path, "/") == 0) {
        st->st_mode = 0755 | S_IFDIR;
        st->st_nlink = 2;
        st->st_size = 0;
 
        list_for_each (n, &entries) {
            struct ou_entry* o = list_entry(n, struct ou_entry, node);
            ++st->st_nlink;
            st->st_size += strlen(o->name);
        }
 
        return 0;
    }
 
    list_for_each (n, &entries) {
        struct ou_entry* o = list_entry(n, struct ou_entry, node);
        if (strcmp(path + 1, o->name) == 0) {
            st->st_mode = o->mode;
            st->st_nlink = 1;
            return 0;
        }
    }
 
    return -ENOENT;
}
 
static int ou_create(const char* path, mode_t mode, struct fuse_file_info* fi)
{
    struct ou_entry* o;
    struct list_node* n;
 
    if (strlen(path + 1) > MAX_NAMELEN)
        return -ENAMETOOLONG;
 
    list_for_each (n, &entries) {
        o = list_entry(n, struct ou_entry, node);
        if (strcmp(path + 1, o->name) == 0)
            return -EEXIST;
    }
 
    o = malloc(sizeof(struct ou_entry));
    strcpy(o->name, path + 1); /* skip leading '/' */
    o->mode = mode | S_IFREG;
    list_add_prev(&o->node, &entries);
 
    return 0;
}
//Unlink is the function used to delete file in file system
static int ou_unlink(const char* path)
{
    struct list_node *n, *p;
 
    list_for_each_safe (n, p, &entries) {
        struct ou_entry* o = list_entry(n, struct ou_entry, node);
        if (strcmp(path + 1, o->name) == 0) {
            __list_del(n);
            free(o);
            return 0;
        }
    }
 
    return -ENOENT;
}
 
static struct fuse_operations oufs_ops = {
    .getattr    =   ou_getattr,
    .readdir    =   ou_readdir,
    .create     =   ou_create,
    .unlink     =   ou_unlink,
};
 
int main(int argc, char* argv[])
{
    list_init(&entries);
 
    return fuse_main(argc, argv, &oufs_ops, NULL);
}