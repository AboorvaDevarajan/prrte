/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University.
 *                         All rights reserved.
 * Copyright (c) 2004-2005 The Trustees of the University of Tennessee.
 *                         All rights reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "orte_config.h"
#include "../src/include/orte_constants.h"


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#include "support.h"
#include "../src/runtime/runtime.h"

#define NUM_ITERS 3

FILE *test_out;

int main (int argc, char* argv[])
{
    int rc, i;

    test_init("orte_start_shut");
    test_out = stderr;
    
    for (i=0; i < NUM_ITERS; i++) {
        fprintf(test_out, "test iteration: %d\n", i);
        if (ORTE_SUCCESS != (rc = orte_init())) {
            fprintf(test_out, "iter %d: couldn't complete init - error code %d\n", i, rc);
            exit(1);
        }
        fprintf(test_out, "\tinit successful\n");
        if (ORTE_SUCCESS != (rc = orte_system_finalize())) {
            fprintf(test_out, "iter %d: couldn't complete finalize - error %d\n", i, rc);
            exit(1);
        }
        fprintf(test_out, "\tfinalize successful\n");
    }
    fprintf(test_out, "orte_start_shut: successful\n");
    
    rc = test_finalize();
    fclose(test_out);
    return rc;
}
