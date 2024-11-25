/**
 * fs.c  -  file system implementation
 *  FSO 24/25
 *  FCT UNL
 *
 *  students:
 *  72360 Jorge Dias
 *
 **/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include "bitmap.h"

#include "fs.h"
#include "disk.h"

/*******
 * FSO FS layout
 * FS block size = disk block size (2K)
 * block#
 * 0            super block (includes the number of inodes)
 * 1 ...        bitmap with free/used blocks begins
 * after bitmap follows blocks with inodes (root dir is inode 0),
 *              assuming on average that each file uses 10 blocks, we need
 *              1 inode per 10 blocks (10%) to fill the disk with files
 * after inodes follows the data blocks
 */

#define BLOCKSZ		(DISK_BLOCK_SIZE)
#define SBLOCK		0	// superblock is in disk block 0
#define BITMAPSTART 1	// free/use block bitmap starts in block 1
#define INODESTART  (rootSB.first_inodeblk)  // inodes start in this block
#define ROOTINO		0 	// root dir is described in inode 0


#define FS_MAGIC    (0xf50f5024) // when formated the SB starts with this number
#define DIRBLOCK_PER_INODE 11	 // direct block's index per inode
#define MAXFILENAME   62         // max name size in a dirent

#define INODESZ		((int)sizeof(struct fs_inode))
#define INODES_PER_BLOCK		(BLOCKSZ/INODESZ)
#define DIRENTS_PER_BLOCK		(BLOCKSZ/sizeof(struct fs_dirent))

#define IFDIR	4	// inode is dir
#define IFREG	8	// inode is regular file


#define FREE 0
#define NOT_FREE 1

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

// Maximum of files open
#define MAX_OPEN_FILES 512


/*** FSO FileSystem in memory structures ***/


// Super block with file system parameters
struct fs_sblock {
    uint32_t magic;     // when formated this field should have FS_MAGIC - This identifies the filesystem type
    uint32_t block_cnt; // number of blocks in disk
    uint16_t bmap_size; // number of blocks with free/use block bitmap
    uint16_t first_inodeblk; // first block with inodes
    uint16_t inode_cnt;      // number of inodes
    uint16_t inode_blocks;   // number of blocks with inodes
    uint16_t first_datablk;  // first block with data or dir
};

// inode describing a file or directory
struct fs_inode {
    uint16_t type;   // node type (FREE, IFDIR, IFREG, etc)
    uint16_t nlinks; // number of links to this inode (not used)
    uint32_t size;   // file size (bytes)
    uint16_t dir_block[DIRBLOCK_PER_INODE]; // direct data blocks
    uint16_t indir_block; // indirect index block
};

// directory entry
struct fs_dirent {
    uint16_t d_ino;           // inode number
    char d_name[MAXFILENAME]; // name (C string)
};

// generic block: a variable of this type may be used as a
// superblock, a block of inodes, a block of dirents or data (byte array)
union fs_block {
    struct fs_sblock super;
    struct fs_inode inode[INODES_PER_BLOCK];
    struct fs_dirent dirent[DIRENTS_PER_BLOCK];
    char data[BLOCKSZ];
};

/**  Super block from mounted File System (Global Variable)**/
struct fs_sblock rootSB;



// Structure to represent an open file
struct open_file {
    struct fs_inode inode;
    int in_use;         // 0 if slot is free, 1 if in use
    int openmode;       // O_RD or O_WR
    int offset;         // Current position in file
};


// Array to store open files
static struct open_file open_files[MAX_OPEN_FILES];


/*****************************************************/

/** check that the global rootSB contains a valid super block of a formated disk
 *  returns -1 if error; 0 if is OK
 */
int check_rootSB() {
    if (rootSB.magic != FS_MAGIC) {
        printf("Unformatted disk!\n");
        return -1;
    }
    return 0;
}

/** finds the disk block number that contains the byte at the given file offset
 *  for the file or directory described by the given inode;
 *  returns the block number
 */
int offset2block(struct fs_inode *inode, int offset) {
    int block = offset / BLOCKSZ;

    if (block < DIRBLOCK_PER_INODE) { // just for direct blocks
        return inode->dir_block[block];
    } else if (block < DIRBLOCK_PER_INODE + BLOCKSZ / 2) {
        // first indirect block index
        uint16_t data[BLOCKSZ / 2];

        disk_read(inode->indir_block, (char*)data);
        printf("returning block %d, indirect %d, %d with content %d\n", block, inode->indir_block,
               block - DIRBLOCK_PER_INODE, data[block - DIRBLOCK_PER_INODE]);
        return data[block - DIRBLOCK_PER_INODE];
    } else {
        printf("offset to big!\n");
        return -1;
    }
}



/** load from disk the inode ino_number into ino (must be an initialized pointer);
 *  returns -1 ino_number outside the existing limits;
 *  returns 0 if inode read. The ino.type == FREE if ino_number is of a free inode
 */
int inode_load(int ino_number, struct fs_inode *ino) {
    union fs_block block;

    if ((unsigned)ino_number > rootSB.inode_cnt * INODES_PER_BLOCK) {
        printf("inode number too big \n");
        ino->type = FREE;
        return -1;
    }
    int inodeBlock = rootSB.first_inodeblk + (ino_number / INODES_PER_BLOCK);
    disk_read(inodeBlock, block.data);
    *ino = block.inode[ino_number % INODES_PER_BLOCK];
    return 0;
}

/** save to disk the inode ino to the ino_number position;
 *  if ino_number is outside limits, nothing is done and returns -1
 *  returns 0 if saved
 */
int inode_save(int ino_number, struct fs_inode *ino) {
    union fs_block block;

    if ((unsigned)ino_number > rootSB.inode_cnt * INODES_PER_BLOCK) {
        printf("inode number too big \n");
        return -1;
    }
    int inodeBlock = rootSB.first_inodeblk + (ino_number / INODES_PER_BLOCK);
    disk_read(inodeBlock, block.data); // read full block
    block.inode[ino_number % INODES_PER_BLOCK] = *ino; // update inode
    disk_write(inodeBlock, block.data); // write block
    return 0;
}


/*****************************************************/

/** dump Super block (usually block 0) from disk to stdout for debugging
 */
void dumpSB(int numb) {
    union fs_block block;

    disk_read(numb, block.data);
    printf("Disk superblock %d:\n", numb);
    printf("    magic = %x\n", block.super.magic);
    printf("    disk size %d blocks\n", block.super.block_cnt);
    printf("    bmap_size: %d\n", block.super.bmap_size);
    printf("    first inode block: %d\n", block.super.first_inodeblk);
    printf("    inode_blocks: %d (%d inodes)\n", block.super.inode_blocks,
           block.super.inode_cnt);
    printf("    first data block: %d\n", block.super.first_datablk);
    printf("    data blocks: %d\n", block.super.block_cnt - block.super.first_datablk );
}


/** prints information details about file system for debugging
 */
void fs_debug() {
    union fs_block block;

    dumpSB(SBLOCK);
    if ( check_rootSB() == -1) return;

    disk_read(SBLOCK, block.data);
    rootSB = block.super;
    printf("**************************************\n");
    printf("blocks in use - bitmap:\n");
    int nblocks = rootSB.block_cnt;
    for (int i = 0; i < rootSB.bmap_size; i++) {
        disk_read(BITMAPSTART + i, block.data);
        bitmap_print(block.data, MIN(BLOCKSZ*8, nblocks));
        nblocks -= BLOCKSZ * 8;
    }
    printf("**************************************\n");
    printf("inodes in use:\n");
    for (int i = 0; i < rootSB.inode_blocks; i++) {
        disk_read(INODESTART + i, block.data);
        for (int j = 0; j < INODES_PER_BLOCK; j++)
            if (block.inode[j].type != FREE)
                printf(" %d: type=%d;", j + i * INODES_PER_BLOCK, block.inode[j].type);
    }
    printf("\n**************************************\n");
}


/** mount root FS;
 *  open device image or create it;
 *  loads superblock from device into global variable rootSB;
 *  returns -1 if error
 */
int fs_mount(char *device, int size) {
    union fs_block block;

    if (rootSB.magic == FS_MAGIC) {
        printf("A disc is already mounted!\n");
        return -1;
    }
    if (disk_init(device, size)<0) return -1; // open disk image or create if it does not exist
    disk_read(SBLOCK, block.data);
    if (block.super.magic != FS_MAGIC) {
        printf("Unformatted disc! Not mounted.\n");
        return 0;
    }
    rootSB = block.super;
    return 0;
}


/*****************************************************/

int name_validity(char *name) {

	//A name should not be bigger than MAXFILENAME
	if (strlen(name) > MAXFILENAME) return -1;

	//A name should not be empty
	if (strlen(name) == 0) return -1;

	//A name should be in ASCII
	for (int i = 0; i < strlen(name); i++) {
		if (name[i] < 32 || name[i] > 126) return -1;
	}

	return 0;
}

int print_ls(char *dirname, int ino_number) {
    struct fs_inode loaded_inode;
    struct fs_inode child_inode;

    /** try to load specified inode by ino_number
    */
    if ( inode_load(ino_number, &loaded_inode) == -1) return -1;

    /** if loaded, iterate its valid dirblocks and print its data
    */
    printf("listing dir %s (inode %d):\n", dirname, ino_number);
    printf("ino:type bytes name\n");
    for (int i = 0; i < DIRBLOCK_PER_INODE; i++) {
        union fs_block dir_block;
        if (loaded_inode.dir_block[i] < disk_size() && loaded_inode.dir_block[i] > 0) {
            disk_read(loaded_inode.dir_block[i], dir_block.data);
            for (int j = 0; j < DIRENTS_PER_BLOCK; j++) {
                /** If a dirent refers to an empty inode, skip to the the next dirblock
                */
                struct fs_dirent *entry = &dir_block.dirent[j];
                if (entry->d_ino != 0 && entry->d_ino < rootSB.inode_cnt && name_validity(entry->d_name) == 0) {
                	inode_load(entry->d_ino, &child_inode);
                        if (child_inode.type == IFDIR || child_inode.type == IFREG) {
                			printf("%3d:%c%9d %s\n", entry->d_ino, child_inode.type == 8 ? 'F' : child_inode.type == 4 ? 'D' : '?', child_inode.size, entry->d_name );
                        }
				}
            }

        }
    }

    return 0;
}

//extract last dir from pathname

char* name_extractor (char *dirname) {

    char *base_name = dirname;

    for (char *c = dirname; *c != '\0'; ++c) {
        if (*c == '/') {
            base_name = c+1;
        }
    }

    return base_name;
}

int find_inode(char *dirname) {
   /** In case dirname refers to the root directory, we know its Inode Number
     */
    if (strcmp(dirname, "/") == 0) {
        return ROOTINO;
    }

    union fs_block s_block;

    /** load SBLOCK data into rootSB variable
     */
    disk_read(SBLOCK, s_block.data);
    rootSB = s_block.super;

    /** loop through the number of inode blocks that exist, listed in the SBLOCK
     */
    for (int i = 0; i < rootSB.inode_blocks; i++) {
        union fs_block inode_block;
        disk_read(INODESTART + i, inode_block.data);
        /** loop through each inode inside the current_block
        */
        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            /** Check if inode is of a directory/file
            */
            if (inode_block.inode[j].type == IFDIR) {
                /** Check all dir_blocks inside the inode that aren't empty
                */
				//printf("inode %d ", j);
                for (int k = 0; k < DIRBLOCK_PER_INODE; k++) {
                    union fs_block dir_block;
                    /** Load dir block and get its dirents - if it's a block WITHIN disk size
                    */
                    if (inode_block.inode[j].dir_block[k] < disk_size() && inode_block.inode[j].dir_block[k] > 0) {
                        disk_read(inode_block.inode[j].dir_block[k], dir_block.data);
						//printf("dir_block %d ", inode_block.inode[j].dir_block[k]);
                        	for (int l = 0; l < DIRENTS_PER_BLOCK; l++) {
                        	/** If a dirent refers to an empty inode, skip to the the next dirblock
                        	*/
                        	struct fs_dirent *entry = &dir_block.dirent[l];
                        	if (entry->d_ino != 0 && entry->d_ino < rootSB.inode_cnt && name_validity(entry->d_name) == 0) {
                                //printf("DIR NAME %s ", entry->d_name);
                        		if (strcmp(name_extractor(dirname), entry->d_name) == 0) {
                        		    return entry->d_ino;
                            	}
							}
                        }
                    }
                }
            }
		}
    }

    return -1;
}

/** list the directory dirname
 */
int fs_ls(char *dirname) {
    if ( check_rootSB() == -1) return -1;

    int inode_number = find_inode(dirname);

    if (inode_number == -1) return -1;

    return print_ls(dirname, inode_number);


    //loads first inode (has infos on root dir) to iroot variable
    /*if (inode_load(ROOTINO, &iroot) != 0) {
        return -1;
    }*/

    /*
    for (int i = ROOTINO; i<INODES_PER_BLOCK; i++) {
        printf("inode %d", iroot.type);
    }
    */


    //fazer parse de dirs
    //ciclo for que corre of blocks dos inodes, à procura dentro de cada inode dos dir_block; verificar se são dirs
        //Se forem dirs, ver se o nome dá match ao pedido. Se sim, fazer fetch do I-NUMBER!
        //Usar I-number

    //disk_read(SBLOCK, block.data);
    //block.super.first_inodeblk;
    //block.super.first_datablk;





    //traverse the filesystem to locate the inode for dirname
    //Read the data blocks pointed to by the directory inode
    //Interpret these blocks as directory entries and list their contents

    return -1;
}


/** open file name;
*  openmode can be O_RD, O_WR or both (O_RD|O_WR)
 *  returns the file descriptor for named file
 */
int fs_open(char *name, int openmode) {
    if (check_rootSB() == -1) return -1;

	//Check if open mode is valid entry
	if ((openmode != O_RD) && (openmode != O_WR) && (openmode != (O_RD|O_WR))) {
        return -1;
    }

    //Assume file descriptor is invalid; Search for a free file descriptor in the open_files array
    int fd = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) {
            fd = i;
            break;
        }
    }
	// If all the slots of the open_files array are filled, returns -1
    if (fd == -1) {
        return -1;  // No free slots available
    }

    int inode_number = find_inode(name);

    if (inode_number == -1) return -1;

    struct fs_inode file_inode;
    if (inode_load(inode_number, &file_inode) < 0) {
        return -1;
    }

    if (file_inode.type != IFREG) {
        return -1;
    }

    open_files[fd].inode = file_inode;
    open_files[fd].in_use = 1;
    open_files[fd].openmode = openmode;
    open_files[fd].offset = 0;
    return fd;

}


/** close file descriptor;
 *  returns 0 or -1 if fd is not a valid file descriptor
 */
int fs_close(int fd) {

  	//Check if fd has a valid value
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -1;

	//if file descriptor [fd] is NOT in_use, return error
  	if (open_files[fd].in_use == 0) return -1;

    //Otherwise, free its space
    //open_files[fd].inode = NULL;
    open_files[fd].in_use = 0;
    //open_files[fd].openmode = NULL;
    open_files[fd].offset = 0;
    return 0;
}


/** reads length bytes into data, starting at filedescriptor's offset
 *  returns the efective number of bytes read (will be 0 at end of file)
 *  returns -1 if error, like invalid fd
 */
int fs_read(int fd, char *data, int length) {
    if (check_rootSB() == -1) return -1;

    //Check if fd has a valid value
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -1;

	//if file descriptor [fd] is NOT in_use, return error
  	if (open_files[fd].in_use == 0) return -1;

    //check if READ permission is up for the file
    if(!(open_files[fd].openmode & O_RD)) return -1;

    //check if inode type is file
 	if (open_files[fd].inode.type != IFREG) return -1;

    //struct fs_inode file_inode = open_files[fd].inode;
    int bytes_read = 0;

    //Access data block
    int offset = open_files[fd].offset;
    int current_data_block_id = offset2block(&open_files[fd].inode, offset);
    int file_size = open_files[fd].inode.size;
    //starting in current block indicated by offset, search through all dir_blocks for the data sequentially
	for (int k = 0; k < DIRBLOCK_PER_INODE && bytes_read < length ; k++) {
		//printf("dir block (first) %d ", open_files[fd].inode.dir_block[k]);

        //Start the loop in the offset block
        if (current_data_block_id == open_files[fd].inode.dir_block[k] && open_files[fd].inode.dir_block[k] < disk_size()) {

			union fs_block curr_data_block;
        	disk_read(open_files[fd].inode.dir_block[k], curr_data_block.data);
            //determine offset in bytes INSIDE block
            int block_offset = offset % BLOCKSZ;
            //bytes to read can't be bigger than remaining bytes in block OR length
			int bytes_to_read = MIN(BLOCKSZ - block_offset, length - bytes_read);
            bytes_to_read = MIN(bytes_to_read, file_size - offset);

            // copy into "data"
        	memcpy(data + bytes_read, curr_data_block.data + block_offset, bytes_to_read);
        	bytes_read += bytes_to_read;
        	offset += bytes_to_read;

			current_data_block_id = offset2block(&open_files[fd].inode, offset);
          	//printf("DIRECT BLOCK :%d: ", current_data_block_id);
          }

//remember updating the open file with offset
    }
    //printf("INDIRECT BLOCK :%d: ", open_files[fd].inode.indir_block);

	open_files[fd].offset = offset;





    return bytes_read;
}
