# FileSystem

Notes
-

Dynamic linking to libraries

Mirroring: double writing info makes READING quicker,
but writin is slower

FileSystem:
Relate names to data!

FILE 1 / SIZE 1 / BLOCK #1

Tools: format, diskpart, mkfs,

Disk Use Bitmap / Directory / Data Area


Processes access files using a common API
(independent of the device)
• Open file to create an access channel, identified by a file
descriptor
• With associated buffers and an offset into file's content
• Read and write data sequentially using the offset
position

Create inodes to act as descriptors for files

