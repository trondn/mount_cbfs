#ifndef CONFIG_H
#define CONFIG_H

struct config {
    const char *cbfs_host;
    const char *cbfs_username;
    const char *cbfs_password;
    const char *couchbase_host;
    const char *couchbase_username;
    const char *couchbase_password;
    const char *couchbase_bucket;
};

extern struct config *get_configuration(void);

#endif
