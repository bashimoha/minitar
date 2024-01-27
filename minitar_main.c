#include <stdio.h>
#include <string.h>

#include "file_list.h"
#include "minitar.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s -c|a|t|u|x -f ARCHIVE [FILE...]\n", argv[0]);
        return 0;
    }
    //  ./minitar <operation> -f <archive_name> <file_name_1> <file_name_2> ... <file_name_n>
    file_list_t files;
    file_list_init(&files);
    const char* cmd = argv[1];
    const char* name = argv[3];
    for(int i=4; i<argc; i++)
    {
        //goodbye.txt adios.txt
        file_list_add(&files, argv[i]);
    }

    if(strcmp(cmd, "-c")==0)
    {
        create_archive(name, &files);
    }
    else if(strcmp(cmd, "-a")==0)
    {

        append_files_to_archive(name, &files);
    }
    else if(strcmp(cmd, "-t")==0)
    {
        get_archive_file_list(name, &files);
        //print the name of the files in the archive
        node_t *current = files.head;
        while (current != NULL) {
            printf("%s\n", current->name);
            current = current->next;
        }
    
    }
    else if(strcmp(cmd, "-u")==0)
    {
        //extracted for tar get archive
        file_list_t res;
        file_list_init(&res);
        get_archive_file_list(name, &res);
        //compare if the conentent of "files" is in "res"
        if(file_list_is_subset(&files, &res))
        {
            //if it is, then append
            append_files_to_archive(name, &files);
        }
        else
        {
            printf("Error: One or more of the specified files is not already present in archive\n");
        }
        file_list_clear(&res);


    }
    else if(strcmp(cmd, "-x")==0)
    {
        extract_files_from_archive(name);
    }
    file_list_clear(&files);
    return 0;
}
