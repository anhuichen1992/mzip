#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(UNIX)
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#define READ_SIZE ((off_t)1*getpagesize())
#endif

#if defined(WINDOWS)
#define off_t off64_t
#define fseeko fseeko64
#define ftello ftello64
#define READ_SIZE ((off_t)1*4096)
#endif

int send_zip(char *name, off_t section_size);
int recv_zip(char *name, off_t section_size);
int copy_file(char *to, off_t to_location, char *name, off_t from_location, off_t size);
int crt_section_num(off_t file_size, off_t section_size);
int get_section_num(char *name);
off_t get_file_size(char *name);

int main(int argc, char *argv[])
{
    int num;
    off_t section_size;
    if(argc >= 4 && (num = atoi(argv[3])) && num > 0 && num <= 4) {
        section_size = ((off_t)num*1024*1024*1024);
    } else {
        section_size = ((off_t)3*1024*1024*1024);
    }
    if(argc >= 3) {
        if(!strcmp(argv[1], "-s")) {
            return send_zip(argv[2], section_size);
        } else if(!strcmp(argv[1], "-r")) {
            return recv_zip(argv[2], section_size);
        }
    }

    printf("Usage:%s -s filename [size][G]\n"\
            "      %s -r filename\n", argv[0], argv[0]);
    return -1;

}

int send_zip(char *name, off_t section_size)
{
    int res = 0;
    char *des_name;
    if(!(des_name = malloc(strlen(name) + 2))) {
        return -1;
    }
    strcpy(des_name, name);
    int des_name_len = strlen(des_name);
    des_name[des_name_len + 1] = '\0';

    off_t file_size = get_file_size(name);
    if(file_size < 0) {
        free(des_name);
        return -1;
    }
    printf("File:%s, Size:%lldB\n", name, file_size);

    int section_num = crt_section_num(file_size, section_size);
    if(section_num < 0) {
        free(des_name);
        return -1;
    }

    printf("Start decompose, please wait ...\n");
    int i;
    off_t location;
    for(i = 0; i < section_num; i++) {
        des_name[des_name_len] = '0' + i;
        remove(des_name);
        location = section_size * i;
        if(section_size * (i + 1) <= file_size) {
            res = copy_file(des_name, 0, name, location, section_size);
        } else {
            res = copy_file(des_name, 0, name, location, file_size - location);
        }
    }

    if(res) {
        printf("ERROR!\n");
        while(i) {
            des_name[des_name_len - 1] = '0' + i;
            remove(des_name);
        }
    } else {
        printf("DONE!\n");
    }

    free(des_name);
    return res;
}

int copy_file(char *to, off_t to_location, char *from, off_t from_location, off_t size)
{
    int res = 0;
    char *buf = malloc(READ_SIZE);
    if(buf == NULL) {
        return -1;
    }

#if defined(WINDOWS)
    FILE *from_file = fopen(from, "rb");
    if(from_file == NULL || fseeko(from_file, from_location, SEEK_SET)) {
        free(buf);
        return -1;
    }
    FILE *to_file = fopen(to, "ab+");
    if(to_file =zo= NULL || fseeko(to_file, to_location, SEEK_SET)) {
        fclose(from_file);
        free(buf);
        return -1;
    }

    while(size >= READ_SIZE) {
        if(fread(buf, READ_SIZE, 1, from_file) != 1 || fwrite(buf, READ_SIZE, 1, to_file) != 1) {
            fclose(to_file);
            fclose(from_file);
            free(buf);
            remove(to);
            return -1;
        }
        size -= READ_SIZE;
    }
    int ch;
    while(size >= 1) {
        if((ch = fgetc(from_file)) == EOF || fputc(ch, to_file) == EOF) {
            fclose(to_file);
            fclose(from_file);
            free(buf);
            remove(to);
            return -1;
        }
        size -= 1;
    }

    fclose(to_file);
    fclose(from_file);
#elif defined(UNIX)
    int from_fd = open(from, O_RDONLY);
    if(from_fd < 0) {
        free(buf);
        return -1;
    }
    int to_fd = open(to, O_RDWR | O_CREAT, 0755);
    if(to_fd < 0) {
        free(buf);
        close(from_fd);
        return -1;
    }
    ftruncate(to_fd, to_location + size);

    char *from_mm, *to_mm;
    int num_max = 1*1024*1024*1024 / READ_SIZE;
    int num_sum = 0;
    while(size >= READ_SIZE) {
        int num = size / READ_SIZE;
        if(num > num_max) {
            num = num_max;
        }
        from_mm = mmap(NULL, num * READ_SIZE, PROT_READ, MAP_PRIVATE, from_fd, from_location + num_sum * READ_SIZE);
        if(from_mm == MAP_FAILED) {
            perror("mmap() from_file failed");
            free(buf);
            close(from_fd);
            close(to_fd);
            return -1;
        }
        to_mm = mmap(NULL, num * READ_SIZE, PROT_WRITE, MAP_SHARED, to_fd, to_location + num_sum * READ_SIZE);
        if(to_mm == MAP_FAILED) {
            perror("mmap() to_file failed");
            free(buf);
            munmap(from_mm, num * READ_SIZE);
            close(from_fd);
            close(to_fd);
            return -1;
        }
        memcpy(to_mm, from_mm, num * READ_SIZE);
        num_sum += num;
        size -= num * READ_SIZE;
        munmap(from_mm, num * READ_SIZE);
        munmap(to_mm, num * READ_SIZE);
    }
    lseek(from_fd, from_location + num_sum * READ_SIZE, SEEK_SET);
    lseek(to_fd, to_location + num_sum * READ_SIZE, SEEK_SET);
    off_t len;
    if(size >= 1) {
        if((len = read(from_fd, buf, READ_SIZE)) > 0 && write(to_fd, buf, len) > 0) {
            ;
        } else {
            perror("tail read() or write() failed");
            res = -1;
        }
    }
    close(from_fd);
    close(to_fd);
#endif
    free(buf);
    return res;
}

int recv_zip(char *name, off_t section_size)
{
    int res = 0;
    char *recv_name;
    if((recv_name = malloc(strlen(name) + 1)) == NULL) {
        return -1;
    }
    strcpy(recv_name, name);
    recv_name[strlen(recv_name) - 1] = '\0';

    int section_num = get_section_num(name);
    if(section_num <= 0) {
        free(recv_name);
        return -1;
    }
    printf("File:%s, Section:%d\n", name, section_num);

    int i;
    printf("Start compose, please wait ...\n");
    remove(recv_name);
    for(i = 0; i < section_num; i++) {
        name[strlen(name) - 1] = '0' + i;
        if(copy_file(recv_name, i * section_size, name, 0, get_file_size(name)) < 0) {
            res = -1;
            break;
        }
    }
    if(res) {
        printf("ERROR!\n");
    } else {
        printf("DONE!\n");
    }

    free(recv_name);
    return res;
}

int crt_section_num(off_t file_size, off_t section_size)
{
    int section_num = file_size / section_size;
    section_num += (section_num * section_size < file_size ? 1 : 0);
    if(section_num > 9) {
        return -1;
    }
    return section_num;
}

int get_section_num(char *name)
{
    char ch = name[strlen(name) - 1];
    if(ch >= '0' && ch <= '9') {
        return ch - '0' + 1;
    } else {
        return -1;
    }
}

off_t get_file_size(char *name)
{
    FILE *file;
    if((file = fopen(name, "r")) == NULL) {
        return -1;
    }

    fseeko(file, 0L, SEEK_END);
    off_t size = ftello(file);
    fclose(file);

    return size;
}
