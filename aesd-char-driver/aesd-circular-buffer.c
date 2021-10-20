/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#define free(x) kfree(x)
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"


#define AESDCHAR_BUFSZ AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED
#define INCWRAP(x) \
	do { \
		x = (x + 1) % AESDCHAR_BUFSZ; \
	} while (0)

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer. 
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
			size_t char_offset, size_t *entry_offset_byte_rtn )
{
	if (buffer->in_offs == buffer->out_offs && !buffer->full) return NULL; // empty buffer

	size_t s_offs = 0; 
	uint8_t i = buffer->out_offs;
	do { // loop guard is false initially if buffer is full
		assert(buffer->entry[i].buffptr);
		size_t new_s = s_offs + buffer->entry[i].size;
		if (new_s > char_offset) break;
		else s_offs = new_s;
		INCWRAP(i);
	} while (i != buffer->in_offs);

	// second condition catches case where first entry contains offset (and so s_offs is 0)
	if (i == buffer->in_offs && s_offs) return NULL; // not found

	*entry_offset_byte_rtn = char_offset - s_offs;
	return &buffer->entry[i];
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
char* aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
	assert(add_entry->buffptr);
	char* rem = NULL;
	if (buffer->full) {
		assert(buffer->in_offs == buffer->out_offs);
		rem = buffer->entry[buffer->out_offs].buffptr;
		INCWRAP(buffer->out_offs);
	}
	
	buffer->entry[buffer->in_offs] = *add_entry;
	INCWRAP(buffer->in_offs);
	buffer->full = (buffer->in_offs == buffer->out_offs);
	return rem;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}

/*
 * Empties @param buffer, freeing all strings it contains.
 */
void aesd_circular_buffer_free(struct aesd_circular_buffer *buffer) {
	if (buffer->out_offs == buffer->in_offs && !full) return;

	do {
		free(buffer->entry[buffer->out_offs].buffptr);
		buffer->entry[buffer->out_offs] = {0};
		INCWRAP(buffer->out_offs);
	} while(buffer->out_offs != buffer->in_offs);
}


