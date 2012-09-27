/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#define FUSE_USE_VERSION 25

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <libcouchbase/couchbase.h>

#include "cJSON.h"
#include "config.h"

static lcb_t instance;
struct config *cfg;

static void error_handler(lcb_t instance, lcb_error_t err, const char *info)
{
    fprintf(stderr, "FATAL! an error occured: %s (%s)\n",
            lcb_strerror(instance, err), info ? info : "none");
    exit(EXIT_FAILURE);
}

struct SizedBuffer {
    char *data;
    size_t size;
};

static void complete_http_callback(lcb_http_request_t req, lcb_t instance,
                                   const void *cookie, lcb_error_t error,
                                   const lcb_http_resp_t *resp)
{
    struct SizedBuffer *sb = (void*)cookie;

    if (error == LCB_SUCCESS) {
        /* Allocate one byte extra for a zero term */
        sb->data = malloc(resp->v.v0.nbytes + 1);
        sb->size = resp->v.v0.nbytes;
        memcpy(sb->data, resp->v.v0.bytes, resp->v.v0.nbytes);
        sb->data[resp->v.v0.nbytes] = '\0';
    }
}

static void initialize(void)
{
    struct lcb_create_st copt;
    lcb_error_t error;

    cfg = get_configuration();

    memset(&copt, 0, sizeof(copt));
    copt.v.v0.host = cfg->couchbase_host;
    copt.v.v0.user = cfg->couchbase_username;
    copt.v.v0.passwd = cfg->couchbase_password;
    copt.v.v0.bucket = cfg->couchbase_bucket;

    if ((error = lcb_create(&instance, &copt)) != LCB_SUCCESS) {
        fprintf(stderr, "Failed to create libcuchbase instance: %s\n",
                lcb_strerror(NULL, error));
        exit(EXIT_FAILURE);
    }

    lcb_behavior_set_syncmode(instance, LCB_SYNCHRONOUS);
    lcb_set_error_callback(instance, error_handler);
    lcb_set_http_complete_callback(instance, complete_http_callback);

    if ((error = lcb_connect(instance)) != LCB_SUCCESS) {
        fprintf(stderr, "Failed to connect to cluster: %s\n",
                lcb_strerror(instance, error));
        exit(EXIT_FAILURE);
    }
}

static lcb_error_t uri_execute_get(const char *uri, struct SizedBuffer *sb) {
    lcb_http_cmd_t cmd = {
        .version = 1,
        .v.v1 = {
            .path = uri,
            .npath = strlen(uri),
            .body = NULL,
            .nbody = 0,
            .method = LCB_HTTP_METHOD_GET,
            .chunked = 0,
            .content_type = "application/x-www-form-urlencoded",
            .host = cfg->cbfs_host,
            .username = cfg->cbfs_username,
            .password = cfg->cbfs_password
        }
    };

    return lcb_make_http_request(instance, sb, LCB_HTTP_TYPE_RAW, &cmd, NULL);
}

static char *ls(const char *path, int *error) {
    *error = 0;
    char buffer[1024];
    int len = snprintf(buffer, sizeof(buffer), "/.cbfs/list%s", path);
    struct SizedBuffer sb = { .data = NULL, .size = 0 };
    lcb_error_t err = uri_execute_get(buffer, &sb);

    if (err != LCB_SUCCESS) {
        fprintf(stderr, "Failed to do http: %s\n", lcb_strerror(instance, err));
    }

    if (sb.data == NULL || memcmp(sb.data, "{\"dirs\":{},\"files\":{}", 21) == 0) {
        free(sb.data);
        *error = -ENOENT;
        return 0;
    }

    return sb.data;
}

static char *mystat(const char *path, int *error) {
    *error = 0;

    struct SizedBuffer sb = {.data = 0, .size = 0};
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "/.cbfs/list%s?includeMeta=true", path);

    lcb_error_t err = uri_execute_get(buffer, &sb);
    if (err != LCB_SUCCESS) {
        fprintf(stderr, "Failed to do http: %s\n", lcb_strerror(instance, err));
        free(sb.data);
        *error = -EIO;
        return NULL;
    }

    if (sb.data == NULL || memcmp(sb.data, "{\"dirs\":{},\"files\":{}", 21) == 0) {
        free(sb.data);
        *error = -ENOENT;
        return NULL;
    }

    return sb.data;
}

static int my_stat(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    int error;
    char *data = mystat(path, &error);
    if (error != 0) {
        return error;
    }

    cJSON *entry = cJSON_Parse(data);
    if (entry == NULL) {
        fprintf(stderr, "Failed to parse\n");
        free(data);
        return -EIO; /* any better? */
    }

    cJSON *dirs = cJSON_GetObjectItem(entry, "dirs");
    cJSON *files = cJSON_GetObjectItem(entry, "files");
    if (dirs == NULL || files == NULL) {
        cJSON_Delete(entry);
        free(data);
        return -EIO; /* any better?? */
    }

    /*
     * This is a directory if it contains something in the dirs array,
     * of if there are multiple entries in the files array
     */
    if (cJSON_GetArraySize(dirs) != 0 || cJSON_GetArraySize(files) > 1){
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        cJSON_Delete(entry);
        free(data);
        return 0;
    }

    assert(cJSON_GetArraySize(files) == 1);

    /*
    ** Ok, this is a regular file unless the only file doesn't point to
    ** myself
    */
    char *me = strrchr(path, '/') + 1; /* (all path's have a leading / ) */
    cJSON *file = files->child;

    if (strcmp(me, file->string) != 0) {
        /* Yeah, this isn't me.. */
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        cJSON_Delete(entry);
        free(data);
        return 0;
    }

    /* Ok, time to look at the attributes... */
    stbuf->st_mode = S_IFREG | 0444;
    stbuf->st_nlink = 1;

    /* Pick out the fields */
    cJSON *obj = file->child;
    while (obj != NULL) {
        if (strcmp("length", obj->string) == 0) {
            if (obj->type == cJSON_Number) {
                stbuf->st_size = obj->valueint;
            } else {
                fprintf(stderr, "Illegal type specified for size\n");
            }
        } else if (strcmp("modified", obj->string) == 0) {
            char *ptr = strchr(obj->valuestring, '.');
            *ptr = '\0';
            struct tm tm;
            if (strptime(obj->valuestring, "%Y-%m-%dT%T", &tm) == NULL) {
                fprintf(stderr, "Failed to parse date\n");
            } else {
                stbuf->st_mtime = mktime(&tm);
                stbuf->st_ctime = stbuf->st_atime = stbuf->st_mtime;
            }
        } else if (strcmp("ctype", obj->string) == 0 ||
                   strcmp("headers", obj->string) == 0 ||
                   strcmp("oid", obj->string) == 0 ||
                   strcmp("revno", obj->string) == 0 ||
                   strcmp("type", obj->string) == 0 ||
                   strcmp("userdata", obj->string) == 0) {
            /* Ignored */
        } else {
            fprintf(stderr, "I've never seen this attribute before: %s\n",
                    obj->string);
        }

        obj = obj->next;
    }

    cJSON_Delete(entry);
    free(data);

    return 0;
}

static int cbfs_getattr(const char *path, struct stat *stbuf)
{
    return my_stat(path, stbuf);
}

static int cbfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    int error;
    char *data = ls(path, &error);
    if (error != 0) {
        fprintf(stderr, "Failed to read\n");
        return error;
    }

    cJSON *json = cJSON_Parse(data);
    if (json == NULL) {
        fprintf(stderr, "Failed to parse\n");
        free(data);
        return -EIO; /* any better? */
    }

    /* I guess I should add these ;-) */
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    /* Add the stuff from the json doc */
    const char * const keys[] = { "dirs", "files" };
    for (int ii = 0; ii < 2; ++ii) {
        cJSON *list = cJSON_GetObjectItem(json, keys[ii]);
        if (list == NULL) {
            continue;
        }

        cJSON *obj = list->child;
        while (obj != NULL) {
            filler(buf, obj->string, NULL, 0);
            obj = obj->next;
        }
    }
    cJSON_Delete(json);
    free(data);

    return 0;
}

static int cbfs_open(const char *path, struct fuse_file_info *fi)
{
    /*
     * Just ensure that you're not trying to write.. after all
     * this is a read only filesystem..
     */
    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        return -EACCES;
    }
    struct stat st;
    if (my_stat(path, &st) != 0) {
        fprintf(stderr, "stat error:\n");
        return -ENOENT;
    }

    return 0;
}

static int cbfs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    struct SizedBuffer sb = {.data = 0, .size = 0};
    lcb_error_t err = uri_execute_get(path, &sb);
    if (err != LCB_SUCCESS || sb.data == NULL) {
        fprintf(stderr, "Failed to do http: %s\n", lcb_strerror(instance, err));
        free(sb.data);
        return -EIO;
    }

    if (offset > sb.size) {
        free(sb.data);
        return -E2BIG;
    }

    if (size > (sb.size - offset)) {
        size = sb.size - offset;
    }

    // Copy the data
    memcpy(buf, sb.data + offset, size);
    free(sb.data);

    return size;
}

static struct fuse_operations cbfs_oper = {
    .getattr = cbfs_getattr,
    .open = cbfs_open,
    .read = cbfs_read,
    .readdir = cbfs_readdir,
};

int main(int argc, char **argv)
{
    initialize();

    return fuse_main(argc, argv, &cbfs_oper);
}
