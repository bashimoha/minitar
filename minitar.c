#include <fcntl.h>
#include <grp.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include "minitar.h"
#include <stdlib.h>
#define NUM_TRAILING_BLOCKS 2
#define MAX_MSG_LEN 512

/*
 * Helper function to compute the checksum of a tar header block
 * Performs a simple sum over all bytes in the header in accordance with POSIX
 * standard for tar file structure.
 */
void compute_checksum(tar_header *header) {
    // Have to initially set header's checksum to "all blanks"
    memset(header->chksum, ' ', 8);
    unsigned sum = 0;
    char *bytes = (char *)header;
    for (int i = 0; i < sizeof(tar_header); i++) {
        sum += bytes[i];
    }
    snprintf(header->chksum, 8, "%07o", sum);
}

/*
 * Populates a tar header block pointed to by 'header' with metadata about
 * the file identified by 'file_name'.
 * Returns 0 on success or -1 if an error occurs
 */
int fill_tar_header(tar_header *header, const char *file_name) {
    memset(header, 0, sizeof(tar_header));
    char err_msg[MAX_MSG_LEN];
    struct stat stat_buf;
    // stat is a system call to inspect file metadata
    if (stat(file_name, &stat_buf) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to stat file %s", file_name);
        perror(err_msg);
        return -1;
    }

    strncpy(header->name, file_name, 100); // Name of the file, null-terminated string
    snprintf(header->mode, 8, "%07o", stat_buf.st_mode & 07777); // Permissions for file, 0-padded octal

    snprintf(header->uid, 8, "%07o", stat_buf.st_uid); // Owner ID of the file, 0-padded octal
    struct passwd *pwd = getpwuid(stat_buf.st_uid); // Look up name corresponding to owner ID
    if (pwd == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up owner name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->uname, pwd->pw_name, 32); // Owner  name of the file, null-terminated string

    snprintf(header->gid, 8, "%07o", stat_buf.st_gid); // Group ID of the file, 0-padded octal
    struct group *grp = getgrgid(stat_buf.st_gid); // Look up name corresponding to group ID
    if (grp == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up group name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->gname, grp->gr_name, 32); // Group name of the file, null-terminated string

    snprintf(header->size, 12, "%011o", (unsigned)stat_buf.st_size); // File size, 0-padded octal
    snprintf(header->mtime, 12, "%011o", (unsigned)stat_buf.st_mtime); // Modification time, 0-padded octal
    header->typeflag = REGTYPE; // File type, always regular file in this project
    strncpy(header->magic, MAGIC, 6); // Special, standardized sequence of bytes
    memcpy(header->version, "00", 2); // A bit weird, sidesteps null termination
    snprintf(header->devmajor, 8, "%07o", major(stat_buf.st_dev)); // Major device number, 0-padded octal
    snprintf(header->devminor, 8, "%07o", minor(stat_buf.st_dev)); // Minor device number, 0-padded octal

    compute_checksum(header);
    return 0;
}

/*
 * Removes 'nbytes' bytes from the file identified by 'file_name'
 * Returns 0 upon success, -1 upon error
 * Note: This function uses lower-level I/O syscalls (not stdio), which we'll learn about later
 */
int remove_trailing_bytes(const char *file_name, size_t nbytes) {
    char err_msg[MAX_MSG_LEN];
    // Note: ftruncate does not work with O_APPEND
    int fd = open(file_name, O_WRONLY);
    if (fd == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s", file_name);
        perror(err_msg);
        return -1;
    }
    //  Seek to end of file - nbytes
    off_t current_pos = lseek(fd, -1 * nbytes, SEEK_END);
    if (current_pos == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to seek in file %s", file_name);
        perror(err_msg);
        close(fd);
        return -1;
    }
    // Remove all contents of file past current position
    if (ftruncate(fd, current_pos) == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to truncate file %s", file_name);
        perror(err_msg);
        close(fd);
        return -1;
    }
    if (close(fd) == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to close file %s", file_name);
        perror(err_msg);
        return -1;
    }
    return 0;
}

int write_zero_bytes(const char* file)
{
    FILE* archive = fopen(file, "ab");
    char zero_block[MAX_MSG_LEN*NUM_TRAILING_BLOCKS];
    memset(zero_block, 0, sizeof(zero_block));
    if (fwrite(zero_block,1, MAX_MSG_LEN*NUM_TRAILING_BLOCKS, archive) != MAX_MSG_LEN*NUM_TRAILING_BLOCKS) {
        return -1;
    }
    fclose(archive);
    return 0;
}


int write_file_to_archive(FILE* archive, tar_header* header, const char* file_name) {
    // Fill the header with metadata for the current file
    fill_tar_header(header, file_name);
    if (fwrite(header, sizeof(tar_header),1, archive) != 1) {
        return -1; 
    }

    // Open the current file for reading
    FILE* curr_file = fopen(file_name, "rb");
    if(curr_file==NULL) {
        return -1; 
    }
    // Allocate a buffer for reading the file contents
    char buffer[MAX_MSG_LEN];
    size_t content_read;
   
    char zero_block[MAX_MSG_LEN*NUM_TRAILING_BLOCKS];
    memset(zero_block, 0, sizeof(zero_block));
    // Read the contents of the file into the buffer, and write the contents and padding to the archive
    while((content_read = fread(buffer, sizeof(char), MAX_MSG_LEN, curr_file)) > 0) {
        size_t padding=0;
        if(content_read < MAX_MSG_LEN) {
            padding = MAX_MSG_LEN - content_read;
        }
        if (fwrite(buffer, sizeof(char), content_read, archive) != content_read) {
            return -1; 
        }
        if (fwrite(zero_block, sizeof(char), padding, archive) != padding) {
            return -1; 
        } 
    }
    
    fclose(curr_file);
    return 0; 
}

int create_archive(const char *archive_name, const file_list_t *files) {
    FILE* archive = fopen(archive_name, "wb");
    if (!archive) {
        return -1;
    }
    // Iterate through the list of files, writing each one to the archive
    node_t *file = files->head;
    tar_header* header = malloc(sizeof(tar_header));
    if (!header) {
        return -1;
    }
    while (file != NULL) {
        if (write_file_to_archive(archive, header, file->name) == -1) {
            free(header);
            fclose(archive);
            return -1;
        }
        file = file->next;
    }
    fclose(archive);
    free(header);
    return write_zero_bytes(archive_name);
}

int append_files_to_archive(const char *archive_name, const file_list_t *files) {
    int zero_bytes = MAX_MSG_LEN*NUM_TRAILING_BLOCKS;
    remove_trailing_bytes(archive_name, zero_bytes);
    FILE* archive = fopen(archive_name, "ab");
    if (!archive) {
        return -1;
    }
     if (!archive) {
        return -1;
    }

    node_t *file = files->head;
    tar_header* header = malloc(sizeof(tar_header));
    if (!header) {
        return -1;
    }

    while (file != NULL) {
        if (write_file_to_archive(archive, header, file->name) == -1) {
            free(header);
            fclose(archive);
            return -1;
        }
        file = file->next;
    }
    fclose(archive);
    free(header);
    return write_zero_bytes(archive_name);
}


int get_archive_file_list(const char *archive_name, file_list_t *files) {
    FILE *archive = fopen(archive_name, "rb");
    if (!archive) {
        return -1;
    }

    tar_header* header = malloc(sizeof(tar_header));
    if (!header) {
        fclose(archive);
        return -1;
    }

    size_t read;
    while ((read = fread(header, sizeof(tar_header), 1, archive)) > 0) {
        if (read != 1) {
            free(header);
            fclose(archive);
            return -1;
        }
        // Extract the file name and add it to the list
        char name[100];
        strncpy(name, header->name, 100);
        file_list_add(files, name);

        // Skip the file content
        int size = strtol(header->size, NULL, 8);
        int padding = 0;
        if (size % MAX_MSG_LEN != 0) {
            padding = MAX_MSG_LEN - (size % MAX_MSG_LEN);
        }
        fseek(archive, size + padding, SEEK_CUR);
    }
    free(header);
    fclose(archive);
    return 0;
}


// Function that extracts the files from an archive
int extract_files_from_archive(const char *archive_name) {
    FILE *archive = fopen(archive_name, "rb");
    if (!archive) {
        return -1;
    }

    tar_header* header = malloc(sizeof(tar_header));
    if (!header) {
        fclose(archive);
        return -1;
    }

    size_t read;
    while ((read = fread(header, sizeof(tar_header), 1, archive)) > 0) {
        if (read != 1) {
            free(header);
            fclose(archive);
            return -1;
        }
        // Extract the file name
        char name[strlen(header->name)+1];
        strncpy(name, header->name, strlen(header->name)+1);
        int size = strtol(header->size, NULL, 8);
        FILE* file = fopen(name, "wb");
        if (!file) {
            free(header);
            fclose(archive);
            return -1;
        }
        int padding = 0;
        int read_bytes;
        char buffer[size];
        while((read_bytes = fread(buffer, sizeof(char), size, archive)) > 0) {
            //write the size of the file in the file
            if (size % MAX_MSG_LEN != 0) {
                padding = MAX_MSG_LEN - (size % MAX_MSG_LEN);
            }
            //write the file content
            fwrite(buffer, sizeof(char), read_bytes, file);
            fseek(archive, padding, SEEK_CUR);
            break;
        }
        fclose(file);
    }

    free(header);
    fclose(archive);
    return 0;
}

