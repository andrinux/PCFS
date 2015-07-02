#ifndef _FILE_H
#define _FILE_H

//Define the structs and functions about file
//Structs: Such as file_t, inode, 
//Functions: such as open/close

#include <sys/types.h>

#define READ (1 << 1)
#define WRITE (1 << 2)


typedef struct {
	char			*filename;
	unsigned int	 filename_hash;

	ino_t		 	ino;		/**< inode */
	nlink_t		 	nlink;		/**< number if hard links */

	int		 		deleted;	/**< Boolean, if set file no longer exists */
	int		 		accesses;	/**< Number of accesses to this file (number of descriptor_t in
					    		the `list` */
	off_t		 	size;		/**< Filesize, if 0 then not read */
	compressor_t	*compressor;	/**< NULL if file isn't compressed */
	off_t		 	skipped;	/**< Number of bytes read and discarded while seeking */
	int		 		dontcompress;
	int		 		type;
	int		 		status;
	
	int		 		 errors_reported;	/**< Number of errors reported for this file */


	struct list_head	head;	/**< Head of descriptor_t. This is needed
					     because truncate has to close all active fd's
					     and do direct_close */
	struct list_head	list;
} file_t;


/*
* Descriptor of A file.
*/

typedef struct {
	file_t		*file;		// link back to file_t, can't be free'd until accesses = 0

	int		 fd;		// file descriptor
	int		 flags;		// Needed when we have to reopen fd, this is fi->flags

	void		*handle;	// for example gzFile
	off_t		 offset;	// offset in file (this if for compression data)

	struct list_head list;
} FILE;

typedef struct
{
	char id[3];			// Special ID contains Compression info
	unsigned char type;	//  compression module
	off_t size;			// Size in uncompressed domain.

} header_t;




#endif