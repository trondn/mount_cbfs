#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"

static struct config cfg = {
    .cbfs_host = "localhost:8484",
    .cbfs_username = NULL,
    .cbfs_password = NULL,
    .couchbase_host = "localhost:8091",
    .couchbase_username = "cbfs",
    .couchbase_password = NULL,
    .couchbase_bucket = "cbfs"
};

static char *get_string(cJSON *obj) {
    if (strlen(obj->valuestring) > 0) {
        return strdup(obj->valuestring);
    }
    return NULL;
}

struct config *get_configuration(void) {
    struct stat st;
    if (stat("config.json", &st) == 0) {
        char *buffer = malloc(st.st_size + 1);
        buffer[st.st_size] = '\0';
        FILE *fp = fopen("config.json", "rb");
        fread(buffer, 1, st.st_size, fp);
        fclose(fp);

        memset(&cfg, 0, sizeof(cfg));

        cJSON *doc = cJSON_Parse(buffer);
        if (doc == NULL) {
            fprintf(stderr, "Failed to parse configuration file\n");
            exit(EXIT_FAILURE);
        }

        cJSON *fields = doc->child;
        while (fields != NULL) {
            if (strcmp(fields->string, "cbfs_host") == 0) {
                cfg.cbfs_host = get_string(fields);
            } else if (strcmp(fields->string, "cbfs_username") == 0) {
                cfg.cbfs_username = get_string(fields);
            } else if (strcmp(fields->string, "cbfs_password") == 0) {
                cfg.cbfs_password = get_string(fields);
            } else if (strcmp(fields->string, "couchbase_host") == 0) {
                cfg.couchbase_host = get_string(fields);
            } else if (strcmp(fields->string, "couchbase_username") == 0) {
                cfg.couchbase_username = get_string(fields);
            } else if (strcmp(fields->string, "couchbase_password") == 0) {
                cfg.couchbase_password = get_string(fields);
            } else if (strcmp(fields->string, "couchbase_bucket") == 0) {
                cfg.couchbase_bucket = get_string(fields);
            } else {
                fprintf(stderr, "Unknown field: %s\n", fields->string);
            }
        }

        cJSON_Delete(doc);
        free(buffer);
    }
    return &cfg;
}
