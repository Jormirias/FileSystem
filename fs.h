#ifndef FS_H
#define FS_H

void fs_debug();
int  fs_mount();
int  fs_ls(char *dirname);

#define O_RD 1
#define O_WR 2
int  fs_open( char *fs_name, int openmode );
int  fs_close( int fd );
int  fs_read( int fd, char *data, int length );


#endif
