#include "devices/block.h"
#include "filesys/off_t.h"

void cache_init (void);
int get_cache_hit_rate (void);
void cache_read_block (block_sector_t sector, void * buffer);
void cache_write_block (block_sector_t sector, const void * buffer);
void cache_read_size (block_sector_t sector, void * buffer, off_t size, off_t offset);
void cache_write_size (block_sector_t sector, const void * buffer, off_t size, off_t offset);
void flush_cache (void);
