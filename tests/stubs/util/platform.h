/*
 * Stand-ins for the handful of libobs platform helpers used by the sources
 * under test, so the pure-logic parts of the plugin can be built and run
 * without OBS installed.
 */

#pragma once

#include <cstdint>
#include <cstdio>

extern "C" {

FILE *os_fopen(const char *path, const char *mode);
int os_fseeki64(FILE *file, int64_t offset, int origin);
int64_t os_get_file_size(const char *path);
bool os_file_exists(const char *path);

struct os_dirent {
	char d_name[256];
	bool directory;
};

typedef struct os_dir os_dir_t;

os_dir_t *os_opendir(const char *path);
struct os_dirent *os_readdir(os_dir_t *dir);
void os_closedir(os_dir_t *dir);
}
