/*
 ============================================================================
 Name        : fso-sh.c
 Author      : vad
 Version     :
 Copyright   : FSO 24/25 - DI-FCT/UNL
 Description : toy file system browser/shell
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "fs.h"

#define BUFSIZE 100

void do_debug(int args) {
    if (args == 1) fs_debug();
    else printf("use: debug\n");
}

void do_ls(int args, char *arg1) {
    if (args<0 || args>2)
        printf("use: ls [dirname]\n");
    else {
        if (args == 1) arg1 = "/";
        if (fs_ls(arg1) < 0) printf("ls failed\n");
    }
}


/** prints help message with available commands
 */
void print_help() {
    printf("Commands:\n");
    printf("    debug\n");
    printf("    ls [<dirname>]\n");
    printf("    cat   <name>\n");
    printf("    copyout <name> <file>\n");
    printf("    help or ?\n");
    printf("    quit or exit\n");
}

/** implementation of file copy from arg1 in the virtual disk
 *  to arg2 in the real OS (arg2 can be /dev/stdout)
 */
void do_copyout(int args, char *arg1, char *arg2) {
    if (args != 3) {
        printf("use: copyout <fsname> <filename>\n");
        return;
    }
    FILE *file = fopen(arg2, "w");
    if (file == NULL) {
        printf("can't open %s: %s\n", arg2, strerror(errno));
        return;
    }
    int fd = fs_open(arg1, O_RD);
    if (fd == -1) {
        printf("can't open %s\n", arg1);
        return;
    }
    int  nbytes = 0;
    char buffer[BUFSIZE];
    int  n;
    while ((n = fs_read(fd, buffer, sizeof(buffer))) > 0) {
        if (n == -1) {
            printf("error in fs_read\n");
            break;
        }
        fwrite(buffer, 1, n, file);
        nbytes += n;
    }
    printf("%d bytes copied\n", nbytes);
    if (fs_close(fd)<0) printf("error in fs_close\n");
    fclose(file);
}


/**
 * MAIN
 * just a shell to browse and test our file system implementation
 */
int main(int argc, char *argv[]) {
    char line[1024];
    char cmd[1024];
    char arg1[1024];
    char arg2[1024];
    int  args, nblocks;

    if (argc != 3 && argc != 2) {
        printf("use: %s diskfile          to use an existing disk\n", argv[0]);
        printf("use: %s diskfile nblocks  to create a new disk with nblocks\n", argv[0]);
        return 1;
    }
    if (argc == 3) nblocks = atoi(argv[2]);
    else nblocks = -1;

    if (fs_mount(argv[1], nblocks) < 0) {
        printf("unable to initialize %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    while (1) {
        printf("fso-sh> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL)
            break;
        args = sscanf(line, "%s %s %s", cmd, arg1, arg2);
        if (args <= 0)
            continue;

        if (!strcmp(cmd, "debug"))
            do_debug(args);
        else if (!strcmp(cmd, "ls"))
            do_ls(args, arg1);
        else if (!strcmp(cmd, "copyout"))
            do_copyout(args, arg1, arg2);
        else if (!strcmp(cmd, "cat")) {
            if (args == 2)
                do_copyout(args + 1, arg1, "/dev/stdout");
            else printf("use: cat <fsname>\n");
        } else if (!strcmp(cmd, "help") || !strcmp(cmd, "?"))
            print_help();
        else if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit"))
            break;
        else {
            printf("unknown command.\n");
            printf("type 'help' or '?' for a list of commands.\n");
        }
    }

    printf("Exiting.\n");
    return EXIT_SUCCESS;
}
