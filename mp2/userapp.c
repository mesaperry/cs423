#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#define PROC_TIME 20
#define ITERATIONS 100000
#define FACTORIAL_TARGET 20

#define FILENAME "/proc/mp2/status"
#define BUFF_SIZE 1024
#define MAX_PID_LEN 20

#define NS_PER_MS 1000000
#define MS_PER_S 1000

int read_file_content(char *buf, unsigned buf_size) {
    FILE *f;
    int i;

    f = fopen(FILENAME, "r");

    /* check module is runnning */
    if (f == NULL) {
        perror("mp2 file not found");
        return EXIT_FAILURE;
    }

    /* load status file into buffer as string */
    i = 0;
    while (!feof(f) && i < buf_size) {
        buf[i++] = fgetc(f);
    }

    /* check file fits into buffer */
    if (i >= buf_size) {
        perror("file doesn't fit into buffer");
        return EXIT_FAILURE;
    }

    /* null terminate string */
    buf[i++] = '\0';

    fclose(f);

    return 0;
}

int write_to_file(char *message) {
    FILE *f;

    f = fopen(FILENAME, "w");

    /* check module is runnning */
    if (f == NULL) {
        perror("mp2 file not found");
        return EXIT_FAILURE;
    }

    /* write */
    if (fprintf(f, "%s", message) < 0) {
        perror("Error writing to file");
        return EXIT_FAILURE;
    }

    fclose(f);

    return 0;
}

unsigned long get_time_diff(struct timespec *start, struct timespec *stop) {
    unsigned long diff_ms;

    /* calculate seconds */
    diff_ms = (stop->tv_sec - start->tv_sec) * MS_PER_S;

    /* add in milliseconds */
    diff_ms += (stop->tv_nsec - start->tv_nsec) / NS_PER_MS;

    return diff_ms;
}

unsigned long compute_factorial(unsigned its, unsigned target) {
    unsigned long num;
    int i, j;

    for (j = 0; j < its; j++) {
        for (i = 1; i <= target; i++) {
            num *= i;
        }
    }

    return num;
}

int main(int argc, char **argv) {
    pid_t pid;
    char buf[BUFF_SIZE];
    char pid_buf[MAX_PID_LEN];
    struct timespec t0, before_job, after_job;
    unsigned long wakeup_time, process_time;
    unsigned long num_jobs;
    int i;

    /* argument check */
    if (argc < 3) {
        errno = EINVAL;
        perror("Not enough args. Need period and number of jobs");
        return EXIT_FAILURE;
    }

    /* get number of jobs from args */
    num_jobs = strtoul(argv[2], NULL, 0);

    pid = getpid();

    /* register self */
    snprintf(buf, sizeof(buf), "R, %d, %s, %d\n", pid, argv[1], (int)PROC_TIME);
    if (write_to_file(buf) != 0) {
        errno = EIO;
        perror("Couldn't write to file");
        return EXIT_FAILURE;
    }

    /* reopen file to read */
    if (read_file_content(buf, sizeof(buf)) != 0) {
        errno = EIO;
        perror("Couldn't read file");
        return EXIT_FAILURE;
    }

    /* convert PID to searchable substring */
    if (snprintf(pid_buf, MAX_PID_LEN, "%d:", pid) < 0) {
        errno = -1;
        perror("Error while converting PID to string");
        return EXIT_FAILURE;
    }

    /* search for PID in status file */
    if (strstr(buf, pid_buf) != NULL) {
        /* PID was found, flag as found */
        ;
    }
    else {
        errno = -1;
        perror("PID not found");
        return EXIT_FAILURE;
    }

    /* get initial time */
    if (clock_gettime(CLOCK_REALTIME, &t0) < 0) {
        errno = -1;
        perror("Clock gettime");
        return EXIT_FAILURE;
    }

    /* yield and signal RMS */
    snprintf(buf, sizeof(buf), "Y, %d\n", pid);
    if (write_to_file(buf) != 0) {
        errno = EIO;
        perror("Couldn't write to file");
        return EXIT_FAILURE;
    }

    /* real-time loop -- run argv[2] number of jobs */
    for (i = 0; i < num_jobs; i++) {
        /* timestamp */
        if (clock_gettime(CLOCK_REALTIME, &before_job) < 0) {
            errno = -1;
            perror("Clock gettime");
            return EXIT_FAILURE;
        }

        /* do work */
        compute_factorial(ITERATIONS, FACTORIAL_TARGET);

        /* timestamp */
        if (clock_gettime(CLOCK_REALTIME, &after_job) < 0) {
            errno = -1;
            perror("Clock gettime");
            return EXIT_FAILURE;
        }
        wakeup_time = get_time_diff(&t0, &before_job);
        process_time = get_time_diff(&before_job, &after_job);

        /* print progress */
        printf("wakeup: %lu, process: %lu\n", wakeup_time, process_time);

        /* yield and signal RMS */
        snprintf(buf, sizeof(buf), "Y, %d\n", pid);
        if (write_to_file(buf) != 0) {
            errno = EIO;
            perror("Couldn't write to file");
            return EXIT_FAILURE;
        }
    }

    /* de-register */
    snprintf(buf, sizeof(buf), "D, %d\n", pid);
    if (write_to_file(buf) != 0) {
        errno = EIO;
        perror("Couldn't write to file");
        return EXIT_FAILURE;
    }

    return 0;
}
