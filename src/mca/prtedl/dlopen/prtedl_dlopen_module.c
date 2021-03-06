/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2015 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2015      Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2016      IBM Corporation.  All rights reserved.
 * Copyright (c) 2018      Amazon.com, Inc. or its affiliates.  All Rights reserved.
 * Copyright (c) 2018-2020 Intel, Inc.  All rights reserved.
 * Copyright (c) 2019      Research Organization for Information Science
 *                         and Technology (RIST).  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "prrte_config.h"

#include <stdlib.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "constants.h"
#include "src/mca/prtedl/prtedl.h"
#include "src/util/argv.h"
#include "src/util/printf.h"

#include "prtedl_dlopen.h"


/*
 * Trivial helper function to avoid replicating code
 */
static void do_dlopen(const char *fname, int flags,
                      void **handle, char **err_msg)
{
    assert(handle);

    *handle = dlopen(fname, flags);

    if (NULL != err_msg) {
        if (NULL != *handle) {
            *err_msg = NULL;
        } else {
            *err_msg = dlerror();
        }
    }
}


static int dlopen_open(const char *fname, bool use_ext, bool private_namespace,
                       prrte_dl_handle_t **handle, char **err_msg)
{
    assert(handle);

    *handle = NULL;

    /* Setup the prtedlopen flags */
    int flags = RTLD_LAZY;
    if (private_namespace) {
        flags |= RTLD_LOCAL;
    } else {
        flags |= RTLD_GLOBAL;
    }

    /* If the caller wants to use filename extensions, loop through
       them */
    void *local_handle = NULL;
    if (use_ext && NULL != fname) {
        int i;
        char *ext;

        for (i = 0, ext = prrte_prtedl_dlopen_component.filename_suffixes[i];
             NULL != ext;
             ext = prrte_prtedl_dlopen_component.filename_suffixes[++i]) {
            char *name;

            prrte_asprintf(&name, "%s%s", fname, ext);
            if (NULL == name) {
                return PRRTE_ERR_IN_ERRNO;
            }

            /* Does the file exist? */
            struct stat buf;
            if (stat(name, &buf) < 0) {
                free(name);
                if (NULL != err_msg) {
                    *err_msg = "File not found";
                }
                continue;
            }

            /* Yes, the file exists -- try to prtedlopen it.  If we can't
               prtedlopen it, bail. */
            do_dlopen(name, flags, &local_handle, err_msg);
            free(name);
            break;
        }
    }

    /* Otherwise, the caller does not want to use filename extensions,
       so just use the single filename that the caller provided */
    else {
        do_dlopen(fname, flags, &local_handle, err_msg);
    }

    if (NULL != local_handle) {
        *handle = calloc(1, sizeof(prrte_dl_handle_t));
        (*handle)->dlopen_handle = local_handle;

#if PRRTE_ENABLE_DEBUG
        if( NULL != fname ) {
            (*handle)->filename = strdup(fname);
        }
        else {
            (*handle)->filename = strdup("(null)");
        }
#endif
    }
    return (NULL != local_handle) ? PRRTE_SUCCESS : PRRTE_ERROR;
}


static int dlopen_lookup(prrte_dl_handle_t *handle, const char *symbol,
                         void **ptr, char **err_msg)
{
    assert(handle);
    assert(handle->dlopen_handle);
    assert(symbol);
    assert(ptr);

    *ptr = dlsym(handle->dlopen_handle, symbol);
    if (NULL != *ptr) {
        return PRRTE_SUCCESS;
    }

    if (NULL != err_msg) {
        *err_msg = dlerror();
    }
    return PRRTE_ERROR;
}


static int dlopen_close(prrte_dl_handle_t *handle)
{
    assert(handle);

    int ret;
    ret = dlclose(handle->dlopen_handle);

#if PRRTE_ENABLE_DEBUG
    free(handle->filename);
#endif
    free(handle);

    return ret;
}

/*
 * Scan all the files in a directory (or path) and invoke a callback
 * on each one.
 */
static int dlopen_foreachfile(const char *search_path,
                              int (*func)(const char *filename, void *data),
                              void *data)
{
    int ret;
    DIR *dp = NULL;
    char **dirs = NULL;
    char **good_files = NULL;

    dirs = prrte_argv_split(search_path, PRRTE_ENV_SEP);
    for (int i = 0; NULL != dirs && NULL != dirs[i]; ++i) {

        dp = opendir(dirs[i]);
        if (NULL == dp) {
            ret = PRRTE_ERR_IN_ERRNO;
            goto error;
        }

        struct dirent *de;
        while (NULL != (de = readdir(dp))) {

            /* Make the absolute path name */
            char *abs_name = NULL;
            prrte_asprintf(&abs_name, "%s/%s", dirs[i], de->d_name);
            if (NULL == abs_name) {
                ret = PRRTE_ERR_IN_ERRNO;
                goto error;
            }

            /* Stat the file */
            struct stat buf;
            if (stat(abs_name, &buf) < 0) {
                free(abs_name);
                ret = PRRTE_ERR_IN_ERRNO;
                goto error;
            }

            /* Skip if not a file */
            if (!S_ISREG(buf.st_mode)) {
                free(abs_name);
                continue;
            }

            /* Find the suffix */
            char *ptr = strrchr(abs_name, '.');
            if (NULL != ptr) {

                /* Skip libtool files */
                if (strcmp(ptr, ".la") == 0 ||
                    strcmp(ptr, ".lo") == 0) {
                    free (abs_name);
                    continue;
                }

                *ptr = '\0';
            }

            /* Have we already found this file?  Or already found a
               file with the same basename (but different suffix)? */
            bool found = false;
            for (int j = 0; NULL != good_files &&
                     NULL != good_files[j]; ++j) {
                if (strcmp(good_files[j], abs_name) == 0) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                prrte_argv_append_nosize(&good_files, abs_name);
            }
            free(abs_name);
        }
        closedir(dp);
    }
    dp = NULL;

    /* Invoke the callback on all the found files */
    if (NULL != good_files) {
        for (int i = 0; NULL != good_files[i]; ++i) {
            ret = func(good_files[i], data);
            if (PRRTE_SUCCESS != ret) {
                goto error;
            }
        }
    }

    ret = PRRTE_SUCCESS;

 error:
    if (NULL != dp) {
        closedir(dp);
    }
    if (NULL != dirs) {
        prrte_argv_free(dirs);
    }
    if (NULL != good_files) {
        prrte_argv_free(good_files);
    }

    return ret;
}


/*
 * Module definition
 */
prrte_prtedl_base_module_t prrte_prtedl_dlopen_module = {
    .open = dlopen_open,
    .lookup = dlopen_lookup,
    .close = dlopen_close,
    .foreachfile = dlopen_foreachfile
};
