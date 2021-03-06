//------------------------------------------------------------------------------
// cctest: test LAGraph_cc_*.c
//------------------------------------------------------------------------------

/*
    LAGraph:  graph algorithms based on GraphBLAS

    Copyright 2019 LAGraph Contributors.

    (see Contributors.txt for a full list of Contributors; see
    ContributionInstructions.txt for information on how you can Contribute to
    this project).

    All Rights Reserved.

    NO WARRANTY. THIS MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. THE LAGRAPH
    CONTRIBUTORS MAKE NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED,
    AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR
    PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF
    THE MATERIAL. THE CONTRIBUTORS DO NOT MAKE ANY WARRANTY OF ANY KIND WITH
    RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.

    Released under a BSD license, please see the LICENSE file distributed with
    this Software or contact permission@sei.cmu.edu for full terms.

    Created, in part, with funding and support from the United States
    Government.  (see Acknowledgments.txt file).

    This program includes and/or can make use of certain third party source
    code, object code, documentation and other files ("Third Party Software").
    See LICENSE file for more details.

*/

// Contributed by Tim Davis, Texas A&M

// Usage: cctest can be used with both stdin or a file as its input.
// We assume by default that the matrix is symmetric. To override this,
// use the file-based input and pass 1 as the last argument.
//
// cctest < matrixmarketfile.mtx
// cctest matrixmarketfile.mtx
// cctest unsymmetric-matrixmarketfile.mtx 0
// cctest symmetric-matrixmarketfile.mtx 1

#include "LAGraph.h"
// #include <sys/time.h>

#define LAGRAPH_FREE_ALL    \
{                           \
    GrB_free (&S) ;         \
    GrB_free (&result) ;    \
    GrB_free (&A) ;         \
}

#define NTHREAD_LIST 4
#define THREAD_LIST 0

// #define NTHREAD_LIST 6
// #define THREAD_LIST 64, 32, 24, 12, 8, 4

/*
double to_sec(struct timeval t1, struct timeval t2)
{
    return
        (t2.tv_sec  - t1.tv_sec ) + 
        (t2.tv_usec - t1.tv_usec) * 1e-6;
}
*/

GrB_Index countCC (GrB_Vector f, GrB_Index n)
{
    GrB_Index nCC = 0;
    GrB_Index *w_val = (GrB_Index*) malloc(sizeof(GrB_Index) * n);
    GrB_Vector_extractTuples(NULL, w_val, &n, f);
    for (GrB_Index i = 0; i < n; i++)
        if (w_val[i] == i)
            nCC += 1;
    free(w_val);
    return nCC;
}

int main (int argc, char **argv)
{
    GrB_Info info ;

    GrB_Matrix A = NULL, S = NULL ;
    GrB_Vector result = NULL ;
    LAGRAPH_OK (LAGraph_init ( )) ;

    int nt = NTHREAD_LIST ;
    int Nthreads [20] = { 0, THREAD_LIST } ;
    int nthreads_max = LAGraph_get_nthreads ( ) ;
    if (Nthreads [1] == 0)
    {
        // create thread list automatically
        Nthreads [1] = nthreads_max ;
        for (int t = 2 ; t <= nt ; t++)
        {
            Nthreads [t] = Nthreads [t-1] / 2 ;
            if (Nthreads [t] == 0) nt = t-1 ;
        }
    }
    printf ("threads to test: ") ;
    for (int t = 1 ; t <= nt ; t++)
    {
        int nthreads = Nthreads [t] ;
        if (nthreads > nthreads_max) continue ;
        printf (" %d", nthreads) ;
    }
    printf ("\n") ;

    FILE *f ;

    GrB_Index n;

    char *matrix_name = (argc > 1) ? argv [1] : "stdin" ;

    // LAGRAPH_OK (LAGraph_mmread (&A, f)) ;
    if (argc > 1)
    {
        // Usage:
        //      ./cctest matrixfile.mtx
        //      ./cctest matrixfile.grb

        // read in the file in Matrix Market format from the input file
        char *filename = argv [1] ;
        printf ("matrix: %s\n", filename) ;

        // find the filename extension
        size_t len = strlen (filename) ;
        char *ext = NULL ;
        for (int k = len-1 ; k >= 0 ; k--)
        {
            if (filename [k] == '.')
            {
                ext = filename + k ;
                printf ("[%s]\n", ext) ;
                break ;
            }
        }
        bool is_binary = (ext != NULL && strncmp (ext, ".grb", 4) == 0) ;

        if (is_binary)
        {
            printf ("Reading binary file: %s\n", filename) ;
            LAGRAPH_OK (LAGraph_binread (&A, filename)) ;
        }
        else
        {
            printf ("Reading Matrix Market file: %s\n", filename) ;
            FILE *f = fopen (filename, "r") ;
            if (f == NULL)
            {
                printf ("Matrix file not found: [%s]\n", filename) ;
                exit (1) ;
            }
            LAGRAPH_OK (LAGraph_mmread(&A, f));
            fclose (f) ;
        }

    }
    else
    {

        // Usage:  ./cctest < matrixfile.mtx
        printf ("matrix: from stdin\n") ;

        // read in the file in Matrix Market format from stdin
        LAGRAPH_OK (LAGraph_mmread(&A, stdin));
    }
    ///////////
    LAGRAPH_OK (GrB_Matrix_nrows (&n, A)) ;
    GrB_Index nvals, svals ;
    LAGr_Matrix_nvals (&nvals, A) ;
    printf ("# of nodes: %lu  # of edges: %lu\n", n, nvals) ;

    // S = A | A'
    GrB_Descriptor desc = 0 ;
    LAGRAPH_OK (GrB_Descriptor_new(&desc)) ;
    LAGRAPH_OK (GrB_Descriptor_set(desc, GrB_INP1, GrB_TRAN)) ;
    LAGRAPH_OK (GrB_Matrix_new (&S, GrB_BOOL, n, n)) ;
    LAGRAPH_OK (GrB_eWiseAdd (S, 0, 0, GrB_LOR, A, A, desc)) ;
    LAGRAPH_FREE (desc) ;
    LAGr_free (&A) ;
    LAGr_Matrix_nvals (&svals, S) ;
    printf ("# of edges in S = A|A':  %lu ", svals) ;
    printf ((svals == nvals) ? "(A symmetric)\n" : "(A unsymmetric)\n") ;

    double tic [2], t1, t2 ;

    #define NTRIALS 16
    printf ("# of trials: %d\n\n", NTRIALS) ;

    bool sanitize = false ;

    GrB_Index nCC;
    for (int trial = 1 ; trial <= nt ; trial++)
    {
        int nthreads = Nthreads [trial] ;
        if (nthreads > nthreads_max) continue ;
        LAGraph_set_nthreads (nthreads) ;

        #if 0
        double t1 = 0 ;
        for (int k = 0 ; k < NTRIALS ; k++)
        {
            LAGraph_tic (tic) ;
            LAGRAPH_OK (LAGraph_cc_fastsv (&result, S, sanitize)) ;
            t1 += LAGraph_toc (tic) ;
            nCC = countCC (result, n) ;
            LAGr_free (&result) ;
        }
        printf("FastSV:   threads: %2d time: %10.4f  # of CC: %lu\n",
            nthreads, t1/ NTRIALS, nCC) ;
        if (n > 1000)
        {
            LAGr_log (matrix_name, "FastSV", nthreads, t1/NTRIALS) ;
        }
        #endif

        t1 = 0 ;
        for (int k = 0 ; k < NTRIALS ; k++)
        {
            LAGraph_tic (tic) ;
            LAGRAPH_OK (LAGraph_cc_fastsv5 (&result, S, sanitize)) ;
            double ttrial = LAGraph_toc (tic) ;
            t1 += ttrial ;
            printf ("SV5:  trial: %2d time: %10.4f sec\n", k, ttrial) ;
            nCC = countCC (result, n) ;
            LAGr_free (&result) ;
        }
        printf("FastSV5:  threads: %2d time: %10.4f  # of CC: %lu\n\n",
            nthreads, t1 / NTRIALS, nCC) ;
        if (n > 1000)
        {
            LAGr_log (matrix_name, "FastSV5", nthreads, t1/NTRIALS) ;
        }

        #if 0
        t1 = 0 ;
        for (int k = 0 ; k < NTRIALS ; k++)
        {
            LAGraph_tic (tic) ;
            LAGRAPH_OK (LAGraph_cc_fastsv5a (&result, &S, sanitize)) ;
            t1 += LAGraph_toc (tic) ;
            nCC = countCC (result, n) ;
            LAGr_free (&result) ;
        }
        printf("FastSV5a: threads: %2d time: %10.4f  # of CC: %lu\n",
            nthreads, t1 / NTRIALS, nCC) ;
        if (n > 1000)
        {
            LAGr_log (matrix_name, "FastSV5a", nthreads, t1/NTRIALS) ;
        }
        #endif

        t1 = 0 ;
        for (int k = 0 ; k < NTRIALS ; k++)
        {
            LAGraph_tic (tic) ;
            LAGRAPH_OK (LAGraph_cc_fastsv5b (&result, &S, sanitize)) ;
            double ttrial = LAGraph_toc (tic) ;
            t1 += ttrial ;
            printf ("SV5b: trial: %2d time: %10.4f sec\n", k, ttrial) ;
            nCC = countCC (result, n) ;
            LAGr_free (&result) ;
        }
        printf("FastSV5b: threads: %2d time: %10.4f  # of CC: %lu\n\n",
            nthreads, t1 / NTRIALS, nCC) ;
        if (n > 1000)
        {
            LAGr_log (matrix_name, "FastSV5b", nthreads, t1/NTRIALS) ;
        }

        /*
        LAGraph_tic (tic) ;
        LAGRAPH_OK (LAGraph_cc_fastsv5 (&result, A, sanitize)) ;
        t1 = LAGraph_toc (tic) ;
        nCC = countCC (result, n) ;
        printf("FastSV5: threads: %2d time: %10.4f  # of CC: %lu\n",
            nthreads, t1, nCC) ;
        LAGr_free (&result) ;
        */

        /*
        LAGraph_tic (tic) ;
        LAGRAPH_OK (LAGraph_cc_boruvka (&result, A, sanitize)) ;
        t2 = LAGraph_toc (tic) ;

        nCC = countCC (result, n) ;
        printf("number of CCs: %lu\n", nCC) ;
        printf("Boruvka: %f\n", t2) ;
        */
        printf("\n");
    }

    LAGRAPH_FREE_ALL ;
    LAGRAPH_OK (GrB_finalize ( )) ;
}

