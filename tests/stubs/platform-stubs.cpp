/*
 * Implementations of the stubbed platform helpers. File access maps onto the
 * C library; directory scanning is stubbed out because the tests exercise
 * playlist ordering rather than the folder walk.
 */

#include <util/platform.h>

#include <sys/stat.h>

extern "C" {

FILE *os_fopen(const char *path, const char *mode)
{
	return fopen(path, mode);
}

int os_fseeki64(FILE *file, int64_t offset, int origin)
{
	return fseeko(file, (off_t)offset, origin);
}

int64_t os_get_file_size(const char *path)
{
	struct stat info;
	return stat(path, &info) == 0 ? (int64_t)info.st_size : -1;
}

bool os_file_exists(const char *path)
{
	struct stat info;
	return stat(path, &info) == 0;
}

os_dir_t *os_opendir(const char *path)
{
	(void)path;
	return nullptr;
}

struct os_dirent *os_readdir(os_dir_t *dir)
{
	(void)dir;
	return nullptr;
}

void os_closedir(os_dir_t *dir)
{
	(void)dir;
}
}
