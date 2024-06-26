/* **********************************************************
 * Copyright (c) 2014 Google, Inc.  All rights reserved.
 * Copyright (c) 2003 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of VMware, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/* Build with:
 * gcc -o pthreads pthreads.c -lpthread -D_REENTRANT -I../lib -L../lib -ldynamo -ldl -lbfd
 * -liberty
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "tools.h"

volatile double pi = 0.0;  /* Approximation to pi (shared) */
pthread_mutex_t pi_lock;   /* Lock for above */
volatile double intervals; /* How many intervals? */

void *
process(void *arg)
{
    char *id = (char *)arg;
    register double width, localsum;
    register int i;
    register int iproc;

#if VERBOSE
    print("\tthread %s starting\n", id);
#endif
    iproc = (*((char *)arg) - '0');

    /* Set width */
    width = 1.0 / intervals;

    /* Do the local computations */
    localsum = 0;
    for (i = iproc; i < intervals; i += 2) {
        register double x = (i + 0.5) * width;
        localsum += 4.0 / (1.0 + x * x);
    }
    localsum *= width;

    /* Lock pi for update, update it, and unlock */
    pthread_mutex_lock(&pi_lock);
    pi += localsum;
    pthread_mutex_unlock(&pi_lock);

#if VERBOSE
    print("\tthread %s exiting\n", id);
#endif
    return (NULL);
}

int
main(int argc, char **argv)
{
    pthread_t thread0, thread1;
    void *retval;

    /* Get the number of intervals */
    if (argc != 2) {
        intervals = 10;
    } else {
        intervals = atoi(argv[1]);
    }

    /* Initialize the lock on pi */
    pthread_mutex_init(&pi_lock, NULL);

    /* Make the two threads */
    if (pthread_create(&thread0, NULL, process, (void *)"0") ||
        pthread_create(&thread1, NULL, process, (void *)"1")) {
        print("%s: cannot make thread\n", argv[0]);
        exit(1);
    }

    /* Join (collapse) the two threads */
    if (pthread_join(thread0, &retval) || pthread_join(thread1, &retval)) {
        print("%s: thread join failed\n", argv[0]);
        exit(1);
    }

    /* Print the result */
    print("Estimation of pi is %16.15f\n", pi);
}
