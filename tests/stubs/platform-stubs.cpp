/*
 * Implementations of the stubbed platform helpers on top of POSIX, so the
 * parts of the plugin under test can touch real files and folders.
 */

#include <util/platform.h>

#include <dirent.h>
#include <sys/stat.h>

#include <cstring>
#include <string>

struct os_dir {
	DIR *dir;
	std::string path;
	struct os_dirent entry;
};

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
	DIR *dir = opendir(path);
	if (!dir)
		return nullptr;

	os_dir_t *handle = new os_dir_t();
	handle->dir = dir;
	handle->path = path;
	return handle;
}

struct os_dirent *os_readdir(os_dir_t *dir)
{
	if (!dir)
		return nullptr;

	struct dirent *entry = readdir(dir->dir);
	if (!entry)
		return nullptr;

	strncpy(dir->entry.d_name, entry->d_name, sizeof(dir->entry.d_name) - 1);
	dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = 0;

	struct stat info;
	const std::string full = dir->path + "/" + entry->d_name;
	dir->entry.directory = stat(full.c_str(), &info) == 0 && S_ISDIR(info.st_mode);

	return &dir->entry;
}

void os_closedir(os_dir_t *dir)
{
	if (!dir)
		return;

	closedir(dir->dir);
	delete dir;
}
}
