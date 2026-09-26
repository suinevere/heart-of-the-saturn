#ifndef CHAINLOAD_H
#define CHAINLOAD_H

#ifdef __cplusplus
extern "C" {
#endif

#define CHAINLOAD_IMAGE "ANOTHER.BIN"

int chainload_available(void);

void chainload_run(void);

#ifdef __cplusplus
}
#endif

#endif
