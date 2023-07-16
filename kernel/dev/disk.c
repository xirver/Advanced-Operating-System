#include <types.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <kernel/dev/disk.h>

#define DEBUG 1

struct disk *disks[MAX_DISKS];
size_t ndisks = 0;


void run_disks(void)
{
	struct disk *disk;
	struct disk_stat stat;
	debug_print("ndisks: %d\n", ndisks);

	for (int i = 0; i < ndisks; ++i) {
		disk = disks[i];
		disk->ops->stat(disk, &stat);
		debug_print("\tdisk %d:\n", i + 1);
		debug_print("\t\tnsectors: %d\n", stat.nsectors);
		debug_print("\t\tsect_size: %d\n", stat.sect_size);
		debug_print("\t\ttotal size: %d megabytes\n", stat.nsectors * stat.sect_size / (1 << 20));
	}
}



int disk_poll(struct disk *disk)
{
	return disk->ops->poll(disk);
}

int disk_stat(struct disk *disk, struct disk_stat *stat)
{
	return disk->ops->stat(disk, stat);
}

int64_t disk_read(struct disk *disk, void *buf, size_t count, uint64_t addr)
{
	return disk->ops->read(disk, buf, count, addr);
}

int64_t disk_write(struct disk *disk, const void *buf, size_t count,
	uint64_t addr)
{
	return disk->ops->write(disk, buf, count, addr);
}

