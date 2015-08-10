
#include "compress_gz.h"
#include "compress_null.h"

compressor_t *choose_compressor(const file_t *file);

/**
 * Decompress file.
 *
 * @param entry Specifies file which will be decompressed
 */
int do_decompress(file_t *file);

/**
 * Compress file.
 *
 * This function is called do_compress because the zlib exports
 * compress name. I think that it is stupid export so common name
 * in library interface.
 *
 * @param entry Specifies file which will be compressed
 */
void do_compress(file_t *file);

/**
 * Test cancel
 *
 * @return This function returns TRUE if compression should be canceled.
 *         Otherwise FALSE is returned.
 */
int compress_testcancel(void *cancel_cookie);
