#ifndef SATURN_DIRENT_H
#define SATURN_DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HOTA_DIR DIR;

struct dirent
{
	char d_name[256];
};

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

#ifdef __cplusplus
}
#endif

#endif
