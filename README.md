# FileSystem

Notes
-
Suggestion:
* Use an array of open files where each element is a structure with all the information about
the open file: a copy of its inode, the openmode and current offset. The file descriptor will be the
respective index of that array.

Structure of Data:
* fs_block is used as a unit of data storage; 
* The first block should hold a struct fs_sblock, 
* THEN one or more fs_block should hold bitmaps, 
* THEN one or more fs_block containing fs_inode, 
* THEN fs_block for data blocks like files and 
directory entries.

Explanation of each Command:
* FS_LS (char *dirname)
  - Traverse the filesystem to locate the inode for dirname
  - Read the data blocks pointed to by the directory inode
  - Interpret these blocks as directory entries and list their contents
  

* FS_OPEN(char *pathname, int openmode)
  - Parse the pathname
  - Traverse the filesystem to locate the inode for filename. (DOES IT ACCEPT ./ and ../ ??? AND CAN YOU OPEN SAME FILE TWICE??)
  - Iterate through open_files[] to find an entry where is_occupied == 0
  - Store the inode copy in the inode field of the entry
  - Set the openmode (read/write)
  - Initialize offset to 0 (start of file)
  - Mark the entry as occupied (is_occupied = 1)
  - Return the index of the allocated slot in open_files[] to be used as file descriptor


* FS_CLOSE(int fd)
  - Ensure the fd is within bounds (0 <= fd < MAX_OPEN_FILES) and is_occupied == 1
  - Mark the slot as free (is_occupied = 0)
  - If the file was opened in write mode, ensure any modified data or inode metadata is written to disk
  - If no other processes are using this inode, remove it from memory.

* FS_READ(int fd, char *data, int length)
  - Check that fd is valid and corresponds to an open file
  - Use open_files[fd] to retrieve the file's inode, offset, and other metadata
  - Determine the file blocks to read based on the current offset and the length requested
  - Fetch the necessary data blocks from disk into a buffer (if not already cached)
  - Copy the requested bytes from the buffer to the data array
  - Increment the offset in open_files[fd] by the number of bytes read

Structure
-
* fso-sh.c – main program. Uses functions from fs.c
* fs.c – file system implementation. This uses disk.c and bitmap.c.
* disk.c – device driver simulation. Offers functions for reading and writing blocks to the virtual disk.
* bitmap.c – bitmap of used/free blocks. Offers functions to set, clear and test bits.

Commands
-
* ls [\<dirname>]
* cat \<name>
* copyout \<name> \<filename>
* help or ?
* exit or quit


Functions
-
FS_LS: int fs_ls(char *dirname)
* This function should print a list of the files in the given directory. Resolves the dirname to its inode number
and then goes through all indexed blocks printing all the valid direntries

FS_OPEN: int fs_open(char *pathname, int openmode)
* Opens the given file for reading or writing, as given in the openmode parameter. Resolves the pathname
  to its inode number.
* (From theory) check path , read i-node into memory, check permissions
  if everything is ok, create a channel (an open file) and return a descriptor (id)

FS_CLOSE: int fs_close(int fd)
* Closes the given file descriptor, freeing its open file representation.
* (From theory) free file descriptor, if last to use the channel, free it; if last to use the i-node,
  may remove it from memory
  if needed, ensures that the buffer content and changes to the i-node are
  written to disk

FS_READ: int fs_read(int fd, char *data, int length)
* This function reads at most length bytes, into data from the file represented by the given file descriptor,
  starting at its current offset.
* request n bytes from offset position to data using channel's buffer
  if needed, request to the disk driver the missing blocks

STRUCT
-
Super block with file system parameters
* struct fs_sblock

Inode describing a file or directory
* struct fs_inode 

Directory entry
* struct fs_dirent 

Generic block: a variable of this type may be used as a
superblock, a block of inodes, a block of dirents or data (byte array)
* union fs_block 

Super block from mounted File System (Global Variable)
* struct fs_sblock rootSB;

