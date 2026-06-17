#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "shmem.h"

static int memfd = -1;
static int use_mock = 0;

#define MAX_MOCK_MAPS 256
static void* mock_maps[MAX_MOCK_MAPS];
static int mock_maps_count = 0;

static void register_mock(void* ptr)
{
	if (mock_maps_count < MAX_MOCK_MAPS)
	{
		mock_maps[mock_maps_count++] = ptr;
	}
}

static int is_mock(void* ptr)
{
	for (int i = 0; i < mock_maps_count; i++)
	{
		if (mock_maps[i] == ptr) return 1;
	}
	return 0;
}

static void unregister_mock(void* ptr)
{
	for (int i = 0; i < mock_maps_count; i++)
	{
		if (mock_maps[i] == ptr)
		{
			mock_maps[i] = mock_maps[--mock_maps_count];
			return;
		}
	}
}

void *shmem_map(uint32_t address, uint32_t size)
{
	if (!use_mock && memfd < 0)
	{
		memfd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
		if (memfd == -1)
		{
			printf("Error: Unable to open /dev/mem! Falling back to virtual mock memory.\n");
			use_mock = 1;
		}
	}

	if (use_mock)
	{
		void* ptr = calloc(1, size);
		register_mock(ptr);
		return ptr;
	}

	void *res = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, address);
	if (res == (void *)-1)
	{
		printf("Error: Unable to mmap (0x%X, %d)! Falling back to virtual mock memory.\n", address, size);
		void* ptr = calloc(1, size);
		register_mock(ptr);
		return ptr;
	}

	return res;
}

int shmem_unmap(void* map, uint32_t size)
{
	if (is_mock(map))
	{
		unregister_mock(map);
		free(map);
		return 1;
	}

	if (munmap(map, size) < 0)
	{
		printf("Error: Unable to unmap(%p, %d)!\n", map, size);
		return 0;
	}

	return 1;
}

int shmem_put(uint32_t address, uint32_t size, void *buf)
{
	void *shmem = shmem_map(address, size);
	if (shmem)
	{
		memcpy(shmem, buf, size);
		shmem_unmap(shmem, size);
	}

	return shmem != 0;
}

int shmem_get(uint32_t address, uint32_t size, void *buf)
{
	void *shmem = shmem_map(address, size);
	if (shmem)
	{
		memcpy(buf, shmem, size);
		shmem_unmap(shmem, size);
	}

	return shmem != 0;
}
