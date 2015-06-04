#ifndef FILE_H
#define FILE_H

typedef struct
{
	char type;		// ID of compression module
	const char *name;
	off_t (*compress)(void *cancel_cookie, int fd_source, int fd_dest);
	off_t (*decompress)(int fd_source, int fd_dest);

	void *(*open) (int fd, const char *mode);
	int (*close)(void *file);
	int (*read)(void *file, void *buf, unsigned int len);
	int (*write)(void *file, void *buf, unsigned int len);

} compressor_t;


typedef struct
{
	char id[3];		// ID of FuseCompress format
	unsigned char type;	// ID of used compression module
	off_t size;		// Uncompressed size of the file in bytes

} __attribute__((packed)) header_t;

/*
* This is file_t, which can be regarded as the inode of a file
* Note that the compresson flag and sequence indicator is included
* Two bit sequence. one is for flags
* For each uncompressed 4kB, there is a flag and indicator, use 
* array to make sure the search complexity is O(1)
*/

typedef struct {
	char		*filename;
	unsigned int	 filename_hash;

	ino_t		 ino;		/**< inode */
	nlink_t		 nlink;		/**< number if hard links */

	int		 deleted;	/**< Boolean, if set file no longer exists */
	int		 accesses;	/**< Number of accesses to this file (number of descriptor_t in
					     the `list` */
	off_t		 size;		/**< Filesize, if 0 then not read */
	compressor_t	*compressor;	/**< NULL if file isn't compressed */
	off_t		 skipped;	/**< Number of bytes read and discarded while seeking */
	int		 dontcompress;
	int		 type;
	int		 status;
	int		 deduped;	/**< File has been deduplicated. */
	
	void**           cache;
	int              cache_size;
	
	int		 errors_reported;	/**< Number of errors reported for this file */

	pthread_mutex_t	lock;
	pthread_cond_t cond;

	struct list_head	head;	/**< Head of descriptor_t. This is needed
					     because truncate has to close all active fd's
					     and do direct_close */
	struct list_head	list;
} file_t;



typedef struct {
	file_t		*file;		// link back to file_t, can't be free'd until accesses = 0

	int		 fd;		// file descriptor
	int		 flags;		// Needed when we have to reopen fd, this is fi->flags

	void		*handle;	// for example gzFile
	off_t		 offset;	// offset in file (this if for compression data)

	struct list_head list;
} descriptor_t;


#endif