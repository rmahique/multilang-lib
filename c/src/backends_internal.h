/* Constructors for each backend, used only by connector.c. */

#ifndef MULTILANG_BACKENDS_INTERNAL_H
#define MULTILANG_BACKENDS_INTERNAL_H

#include "backend.h"

ml_status ml_sqlite_backend_open(const char *path, ml_backend **out, char *errbuf, size_t errbuf_len);

ml_status ml_filesystem_backend_open(const char *root, ml_backend **out, char *errbuf, size_t errbuf_len);

ml_status ml_postgres_backend_open(const char *host, int port, const char *user,
                                    const char *password, const char *database,
                                    const char *sslmode, ml_backend **out,
                                    char *errbuf, size_t errbuf_len);

ml_status ml_mysql_backend_open(const char *host, int port, const char *user,
                                 const char *password, const char *database,
                                 const char *sslmode, ml_backend **out,
                                 char *errbuf, size_t errbuf_len);

#endif /* MULTILANG_BACKENDS_INTERNAL_H */
