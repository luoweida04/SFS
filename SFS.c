#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <malloc.h>
#define FS_BLOCK_SIZE 512          // 块大小512字节
#define SUPER_BLOCK 1              // 一个超级块
#define FIRST_BLK 518              // 数据区的第一块块号
#define DATASIZE 15866             // 数据区大小，以块为单位
#define FIRST_INODE 6              // inode区起始块号
#define INODE_AREA_SIZE 512        // inode区大小，以块为单位
#define FIRST_BLK_OF_INODEBITMAP 1 // inode位图区起始块号
#define INODEBITMAP_SIZE 1         // inode位图区大小，以块为单位
#define FIRST_BLK_OF_DATABITMAP 2  // 数据块位图起始块号
#define DATABITMAP_SIZE 4          // 数据块位图大小，以块为单位
#define MAX_FILENAME 8
#define MAX_EXTENSION 3
long TOTAL_BLOCK_NUM;
// 我的8M磁盘文件为:"/home/luoweida/桌面/SFS/diskimg"
char *disk_path = "/home/luoweida/桌面/SFS/diskimg";
// 超级块中记录的，大小为 72 bytes（9个long），占用1块磁盘块
struct sb
{
    long fs_size;                  // 文件系统的大小，以块为单位
    long first_blk;                // 数据区的第一块块号，根目录也放在此
    long datasize;                 // 数据区大小，以块为单位
    long first_inode;              // inode区起始块号
    long inode_area_size;          // inode区大小，以块为单位
    long first_blk_of_inodebitmap; // inode位图区起始块号
    long inodebitmap_size;         // inode位图区大小，以块为单位
    long first_blk_of_databitmap;  // 数据块位图起始块号
    long databitmap_size;          // 数据块位图大小，以块为单位
};
struct inode
{
    short int st_mode;       /* 权限，2字节 */
    short int st_ino;        /* i-node号，2字节 */
    char st_nlink;           /* 连接数，1字节 */
    uid_t st_uid;            /* 拥有者的用户 ID ，4字节 */
    gid_t st_gid;            /* 拥有者的组 ID，4字节  */
    off_t st_size;           /*文件大小，4字节 */
    struct timespec st_atim; /* 16个字节time of last access */
    /*磁盘地址，14字节, addr[0]-addr[3]是直接地址，addr[4]是一次间接，addr[5]是二次间接，addr[6]是三次间接。*/
    short int addr[7];
};
// 记录文件信息的数据结构，每个目录项为16B
struct file_directory
{
    char fname[8];    // 文件名 ，8字节
    char fext[3];     // 扩展名 ，3字节
    short int st_ino; // i-node号，2字节，实际使用12位
    char reserve[3];  // 备用3字节
};
// 文件内容存放用到的数据结构，大小为 512 bytes，1块磁盘块
struct data_block
{
    int size;
    char data[508];
};
// 辅助函数声明
void get_inode(short int st_ino, struct inode *i);
int get_inode_and_dir_bypath(char *path, struct inode *res_inode, struct file_directory *res_dir);
int insert_new_dir(struct inode *par_inode, struct file_directory *new_dir, int flag);
int insert_new_dir_help(long blk_no, struct file_directory *new_dir, int flag);
int read_cpy_data_block(long blk_no, struct data_block *data_blk);
int write_data_block(long blk_no, struct data_block *data_blk);
int enlarge_blk();
int free_blk(short int blk_no);
int free_inode(int st_ino);
int path_is_emp(const char *path);
int foreach_datablk_remove_dir(short int blk_no, struct file_directory *attr);
int remove_dir(struct inode *par_inode, struct file_directory *dir);
int remove_inode(struct inode *this);
int check_inode_emp(struct inode *this);
int read_on_blk(short int blk_no, int flag, char *buf, off_t *offset, int *temp_size, size_t size);
int write_on_blk(short int blk_no, int flag, char *buf, off_t *offset, int *temp_size, size_t size);
// 功能函数声明
int get_fd_to_attr(const char *path, struct file_directory *attr);
int create_file_dir(const char *path, int flag);
int remove_file_dir(const char *path, int flag);
/***************************************************************************************************************************/
// 下面是一些读写文件的辅助函数：
//  根据inode号获取inode
void get_inode(short int st_ino, struct inode *i)
{
    FILE *fp = NULL;
    fp = fopen(disk_path, "r+");
    if (fp == NULL)
    {
        fprintf(stderr, "错误：打开文件失败，文件不存在，函数结束返回\n");
        return;
    }
    fseek(fp, 6 * FS_BLOCK_SIZE + st_ino * 64, SEEK_SET); // 找到对应的inode
    fread(i, sizeof(struct inode), 1, fp);
}
// 根据文件路径获取inode
int get_inode_and_dir_bypath(char *path, struct inode *res_inode, struct file_directory *res_dir)
{
    printf("get_inode_and_dir_bypath函数开始  查找的文件路径：%s\n\n", path);
    struct inode *temp_inode = malloc(sizeof(struct inode));
    struct data_block *data_blk = malloc(sizeof(struct data_block));
    struct file_directory *temp_dir;
    if (strcmp(path, "/") == 0)
    { // 如果是根目录, 根目录没有目录项
        // res_inode = temp_inode;
        get_inode(0, res_inode);
        res_dir->st_ino = 0;
        strcpy(res_dir->fname, "/");
        printf("这是根目录，函数调用成功，返回\n");
        return 0;
    }
    get_inode(0, temp_inode);
    FILE *fp = NULL;
    fp = fopen(disk_path, "r+");
    if (fp == NULL)
    {
        fprintf(stderr, "错误：打开文件失败，文件不存在，函数结束返回\n");
        return -1;
    }
    printf("check1\n\n");
    char *temp_path = strdup(path); // 拷贝一份
    temp_path++;
    int last_target = 0;
    while (temp_path != NULL)
    {
        // 先测试一下是否到达目标
        char *test_temp_path = strchr(temp_path, '/');
        char *next_file_name = strdup(temp_path);
        char *next_file_ext = '\0';
        printf("check1-1 %s\n", temp_path);
        if (test_temp_path == NULL)
        { // 到达目标, 此时temp_path就是目标名
            last_target = 1;
            next_file_name = temp_path; // 此时next_file_name为目录名字
            temp_path = strchr(temp_path, '/');
            printf("到达目标\n\n");
        }
        else
        {
            // 还未到达最后目标
            temp_path = strchr(temp_path, '/');
            char *next_p = next_file_name;
            next_p = strchr(next_p, '/');
            if (next_p != NULL)
                *next_p = '\0'; // 此时next_file_name为下级目录名字
            printf("还未到达目标\n\n");
        }
        printf("check2  完整文件名: %s\n", next_file_name);
        if (last_target == 1)
        {
            char *t = strchr(next_file_name, '.');
            if (t != NULL)
            {
                *t = '\0';
                next_file_ext = ++t;
            }
        }
        printf("文件名：%s, 拓展名: %s\n\n", next_file_name, next_file_ext);
        int flag = 0; // 是否找到当前目录的inode
        for (int i = 0; i < 7 && temp_inode->addr[i] != -1 && flag == 0; i++)
        { // 找到下一级目录inode
            if (i < 4)
            { // 直接索引
                if (read_cpy_data_block(FIRST_BLK + temp_inode->addr[i], data_blk) == -1)
                    return -1;
                temp_dir = (struct file_directory *)data_blk->data;
                int pos = 0;
                printf("check333  文件名: %s\n\n", next_file_name);
                while (pos < data_blk->size)
                {
                    printf("check4  temp_dir->name: %s, 文件名: %s\n\n", temp_dir->fname, next_file_name);
                    if (strcmp(temp_dir->fname, next_file_name) == 0 && (next_file_ext == '\0' || strcmp(temp_dir->fext, next_file_ext) == 0))
                    {
                        printf("找到目录%s\n\n", next_file_name);
                        if (last_target == 1)
                        { // 已经是最后一级路径了
                            get_inode(temp_dir->st_ino, res_inode);
                            printf("找到的目录inode号： %d\n\n", temp_dir->st_ino);
                            strcpy(res_dir->fname, temp_dir->fname);
                            strcpy(res_dir->fext, temp_dir->fext);
                            strcpy(res_dir->reserve, temp_dir->reserve);
                            res_dir->st_ino = temp_dir->st_ino;
                            fclose(fp);
                            return 0;
                        }
                        get_inode(temp_dir->st_ino, temp_inode);
                        flag = 1;
                        break;
                    }
                    pos += sizeof(struct file_directory);
                    temp_dir++;
                }
            }
            else if (i == 4)
            { // 一级间址
                struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + temp_inode->addr[i], addr1_blk) == -1)
                    return -1;
                short int *addr1 = addr1_blk->data;
                int pos1 = 0;
                while (pos1 < addr1_blk->size)
                {
                    if (read_cpy_data_block(FIRST_BLK + *addr1, data_blk) == -1)
                        return -1;
                    temp_dir = (struct file_directory *)data_blk->data;
                    int pos = 0;
                    printf("check333  文件名: %s\n\n", next_file_name);
                    while (pos < data_blk->size)
                    {
                        printf("check4  temp_dir->name: %s, 文件名: %s\n\n", temp_dir->fname, next_file_name);
                        if (strcmp(temp_dir->fname, next_file_name) == 0 && (next_file_ext == '\0' || strcmp(temp_dir->fext, next_file_ext) == 0))
                        {
                            printf("找到目录%s\n\n", next_file_name);
                            if (last_target == 1)
                            { // 已经是最后一级路径了
                                get_inode(temp_dir->st_ino, res_inode);
                                printf("找到的目录inode号： %d\n\n", temp_dir->st_ino);
                                strcpy(res_dir->fname, temp_dir->fname);
                                strcpy(res_dir->fext, temp_dir->fext);
                                strcpy(res_dir->reserve, temp_dir->reserve);
                                res_dir->st_ino = temp_dir->st_ino;
                                fclose(fp);
                                return 0;
                            }
                            get_inode(temp_dir->st_ino, temp_inode);
                            flag = 1;
                            pos1 = addr1_blk->size; // 方便退出外层循环
                            break;
                        }
                        pos += sizeof(struct file_directory);
                        temp_dir++;
                    }
                    addr1++;
                    pos1 += sizeof(short int);
                }
            }
            else if (i == 5)
            { // 二级间址
                struct data_block *addr2_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + temp_inode->addr[i], addr2_blk) == -1)
                    return -1;
                short int *addr2 = addr2_blk->data;
                int pos2 = 0;
                while (pos2 < addr2_blk->size)
                {
                    struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                        return -1;
                    short int *addr1 = addr1_blk->data;
                    int pos1 = 0;
                    while (pos1 < addr1_blk->size)
                    {
                        if (read_cpy_data_block(FIRST_BLK + *addr1, data_blk) == -1)
                            return -1;
                        temp_dir = (struct file_directory *)data_blk->data;
                        int pos = 0;
                        printf("check333  文件名: %s\n\n", next_file_name);
                        while (pos < data_blk->size)
                        {
                            printf("check4  temp_dir->name: %s, 文件名: %s\n\n", temp_dir->fname, next_file_name);
                            if (strcmp(temp_dir->fname, next_file_name) == 0 && (next_file_ext == '\0' || strcmp(temp_dir->fext, next_file_ext) == 0))
                            {
                                printf("找到目录%s\n\n", next_file_name);
                                if (last_target == 1)
                                { // 已经是最后一级路径了
                                    get_inode(temp_dir->st_ino, res_inode);
                                    printf("找到的目录inode号： %d\n\n", temp_dir->st_ino);
                                    strcpy(res_dir->fname, temp_dir->fname);
                                    strcpy(res_dir->fext, temp_dir->fext);
                                    strcpy(res_dir->reserve, temp_dir->reserve);
                                    res_dir->st_ino = temp_dir->st_ino;
                                    fclose(fp);
                                    return 0;
                                }
                                get_inode(temp_dir->st_ino, temp_inode);
                                flag = 1;
                                pos1 = addr1_blk->size; // 方便退出外层循环
                                pos2 = addr2_blk->size;
                                break;
                            }
                            pos += sizeof(struct file_directory);
                            temp_dir++;
                        }
                        addr1++;
                        pos1 += sizeof(short int);
                    }
                    addr2++;
                    pos2 += sizeof(short int);
                }
            }
            else
            { // 三级间址
                struct data_block *addr3_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + temp_inode->addr[i], addr3_blk) == -1)
                    return -1;
                short int *addr3 = addr3_blk->data;
                int pos3 = 0;
                while (pos3 < addr3_blk->size)
                {
                    struct data_block *addr2_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + *addr3, addr2_blk) == -1)
                        return -1;
                    short int *addr2 = addr2_blk->data;
                    int pos2 = 0;
                    while (pos2 < addr2_blk->size)
                    {
                        struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                        if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                            return -1;
                        short int *addr1 = addr1_blk->data;
                        int pos1 = 0;
                        while (pos1 < addr1_blk->size)
                        {
                            if (read_cpy_data_block(FIRST_BLK + *addr1, data_blk) == -1)
                                return -1;
                            temp_dir = (struct file_directory *)data_blk->data;
                            int pos = 0;
                            printf("check333  文件名: %s\n\n", next_file_name);
                            while (pos < data_blk->size)
                            {
                                printf("check4  temp_dir->name: %s, 文件名: %s\n\n", temp_dir->fname, next_file_name);
                                if (strcmp(temp_dir->fname, next_file_name) == 0 && (next_file_ext == '\0' || strcmp(temp_dir->fext, next_file_ext) == 0))
                                {
                                    printf("找到目录%s\n\n", next_file_name);
                                    if (last_target == 1)
                                    { // 已经是最后一级路径了
                                        get_inode(temp_dir->st_ino, res_inode);
                                        printf("找到的目录inode号： %d\n\n", temp_dir->st_ino);
                                        strcpy(res_dir->fname, temp_dir->fname);
                                        strcpy(res_dir->fext, temp_dir->fext);
                                        strcpy(res_dir->reserve, temp_dir->reserve);
                                        res_dir->st_ino = temp_dir->st_ino;
                                        fclose(fp);
                                        return 0;
                                    }
                                    get_inode(temp_dir->st_ino, temp_inode);
                                    flag = 1;
                                    pos1 = addr1_blk->size; // 方便退出外层循环
                                    pos2 = addr2_blk->size;
                                    pos3 = addr3_blk->size;
                                    break;
                                }
                                pos += sizeof(struct file_directory);
                                temp_dir++;
                            }
                            addr1++;
                            pos1 += sizeof(short int);
                        }
                        addr2++;
                        pos2 += sizeof(short int);
                    }
                    addr3++;
                    pos3 += sizeof(short int);
                }
            }
        }

        if (temp_path != NULL)
        {
            printf("check5 %s\n\n", temp_path);
            temp_path++;
        }
    }
    free(data_blk);
    printf("未找到文件 %s\n\n", path);
    return -1;
}
// 在块中插入新目录项，失败返回-1,成功返回0(已写入)
int insert_new_dir_help(long blk_no, struct file_directory *new_dir, int flag)
{ // 1文件，2目录
    printf("insert_new_dir_help开始\n\n");
    struct data_block *data_blk = malloc(sizeof(struct data_block));
    struct file_directory *temp_dir;
    printf("check1\n\n");
    if (read_cpy_data_block(blk_no, data_blk) == -1)
        return -1;
    temp_dir = (struct file_directory *)data_blk->data;
    int pos = 0;
    while (pos < data_blk->size)
    {
        printf("pos = %d, data_blk->size = %d\n\n", pos, data_blk->size);
        temp_dir++;
        pos += sizeof(struct file_directory);
    }
    if (pos < 496)
    { // 可以直接追加新的
        printf("sizeof(struct file_directory)=%d\n", sizeof(struct file_directory));
        data_blk->size = data_blk->size + sizeof(struct file_directory);
        strcpy(temp_dir->fext, new_dir->fext);
        strcpy(temp_dir->fname, new_dir->fname);
        temp_dir->st_ino = new_dir->st_ino;
        strcpy(temp_dir->reserve, new_dir->reserve);
        if (write_data_block(blk_no, data_blk) == -1)
            return -1;
        printf("块中有剩余空间，新增目录项 %s 成功\n\n", new_dir->fname);
        // 这里新增inode
        FILE *fp = NULL;
        fp = fopen(disk_path, "r+");
        if (fp == NULL)
        {
            printf("打开文件失败，文件不存在\n");
            return 0;
        }
        struct inode *new_inode = malloc(sizeof(struct inode)); // 动态内存分配，申请root_inode
        if (flag == 2)
            new_inode->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR; // 目录文件，可读/写/执行
        else
            new_inode->st_mode = S_IFREG | S_IRUSR | S_IWUSR; // 普通文件，可读/写
        new_inode->st_ino = temp_dir->st_ino;                 /* i-node号，2字节 */
        new_inode->st_nlink = 0;                              /* 连接数，1字节 */
        new_inode->st_uid = getuid();                         /* 拥有者的用户 ID ，4字节 */
        new_inode->st_gid = getgid();                         /* 拥有者的组 ID，4字节  */
        new_inode->st_size = 0;                               /*文件大小，0字节 */
        timespec_get(&(new_inode->st_atim), TIME_UTC);
        // clock_gettime(CLOCK_REALTIME, &(new_inode->st_atim));

        memset(new_inode->addr, -1, sizeof(new_inode->addr));
        new_inode->addr[0] = enlarge_blk();
        if (new_inode->addr[0] == -1)
        {
            printf("新增数据块失败\n\n");
            fclose(fp);
            return -1;
        }
        fseek(fp, FIRST_INODE * FS_BLOCK_SIZE + temp_dir->st_ino * 64, SEEK_SET);
        fwrite(new_inode, sizeof(struct inode), 1, fp);
        fclose(fp);
        printf("inode添加成功!\n\n");
        return 0;
    }
    printf("块中没有空间，新增目录项失败\n\n");
    return -1;
}
// 插入新目录项
int insert_new_dir(struct inode *par_inode, struct file_directory *new_dir, int flag)
{
    //
    printf("insert_new_dir开始\n\n");
    for (int i = 0; i < 7; i++)
    {
        printf("当前i = %d    par_inode->addr[i] = %d\n\n", i, par_inode->addr[i]);
        if (i < 4)
        {
            if (par_inode->addr[i] == -1)
            {
                par_inode->addr[i] = enlarge_blk(); // 新增数据块
                if (par_inode->addr[i] != -1 && insert_new_dir_help(FIRST_BLK + par_inode->addr[i], new_dir, flag) == 0)
                    return 0;
                else
                    return -1;
            }
            else if (insert_new_dir_help(FIRST_BLK + par_inode->addr[i], new_dir, flag) == 0)
                return 0;
        }
        else if (i == 4)
        {
            if (par_inode->addr[i] == -1)
            {                                       // 建立一级间址
                par_inode->addr[i] = enlarge_blk(); // 新增一级地址块
                if (par_inode->addr[i] != -1)
                {
                    struct data_block *new_addr1_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + par_inode->addr[i], new_addr1_blk) == -1)
                        return -1;
                    short int *addr1 = (short int *)new_addr1_blk->data;
                    *addr1 = enlarge_blk(); // 新增数据块
                    new_addr1_blk->size += sizeof(short int);
                    if (write_data_block(FIRST_BLK + par_inode->addr[i], new_addr1_blk) == -1)
                        return -1;
                    if (*addr1 != -1 && insert_new_dir_help(FIRST_BLK + *addr1, new_dir, flag) == 0)
                        return 0;
                    else
                        return -1;
                }
                else
                {
                    printf("新增一级地址块失败\n\n");
                    return -1;
                }
            }
            // 如果已经有一级地址块
            struct data_block *addr1_blk = malloc(sizeof(struct data_block));
            read_cpy_data_block(FIRST_BLK + par_inode->addr[i], addr1_blk);
            short int *addr1 = (short int *)addr1_blk->data;
            int addr1_pos = 0;
            while (addr1_pos < addr1_blk->size)
            {
                if (insert_new_dir_help(FIRST_BLK + *addr1, new_dir, flag) != 0)
                { // 失败
                    addr1++;
                    addr1_pos += sizeof(short int);
                }
                else
                    return 0;
            }
            // 前面所有地址指向的数据块都满了，那么看看是否能新增地一级址
            if (addr1_blk->size < 508)
            { // 可以新增地址, 并增加一个数据块
                *addr1 = enlarge_blk();
                if (*addr1 != -1)
                {
                    addr1_blk->size += sizeof(short int);
                    if (write_data_block(FIRST_BLK + par_inode->addr[i], addr1_blk) == -1)
                        return -1;
                    if (insert_new_dir_help(FIRST_BLK + *addr1, new_dir, flag) == -1)
                        return -1;
                    else
                        return 0;
                }
            }
        }
        else if (i == 5)
        {
            if (par_inode->addr[i] == -1)
            {                                       // 建立二级间址
                par_inode->addr[i] = enlarge_blk(); // 新增二级地址块
                if (par_inode->addr[i] != -1)
                {
                    struct data_block *new_addr2_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + par_inode->addr[i], new_addr2_blk) == -1)
                        return -1;
                    short int *addr2 = (short int *)new_addr2_blk->data;
                    *addr2 = enlarge_blk(); // 新增一级地址块
                    new_addr2_blk->size += sizeof(short int);
                    if (write_data_block(FIRST_BLK + par_inode->addr[i], new_addr2_blk) == -1)
                        return -1;
                    if (*addr2 != -1)
                    {
                        struct data_block *new_addr1_blk = malloc(sizeof(struct data_block));
                        if (read_cpy_data_block(FIRST_BLK + *addr2, new_addr1_blk) == -1)
                            return -1;
                        short int *addr1 = (short int *)new_addr1_blk->data;
                        *addr1 = enlarge_blk(); // 新增数据块
                        new_addr2_blk->size += sizeof(short int);
                        if (write_data_block(FIRST_BLK + *addr2, new_addr1_blk) == -1)
                            return -1;
                        if (*addr1 != -1 && insert_new_dir_help(FIRST_BLK + *addr1, new_dir, flag) == 0)
                            return 0;
                        else
                            return -1;
                    }
                }
                else
                {
                    printf("新增二级地址块失败\n\n");
                    return -1; // 新增地址块失败
                }
            }
            // 如果已经有二级间址块
            struct data_block *addr2_blk = malloc(sizeof(struct data_block));
            read_cpy_data_block(FIRST_BLK + par_inode->addr[i], addr2_blk);
            short int *addr2 = (short int *)addr2_blk->data;
            int addr2_pos = 0;
            while (addr2_pos < addr2_blk->size)
            { // 遍历所有二级地址，即所有一级地址块
                struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                read_cpy_data_block(FIRST_BLK + addr2, addr1_blk);
                short int *addr1 = (short int *)addr1_blk->data;
                int addr1_pos = 0;
                while (addr1_pos < addr1_blk->size)
                { // 遍历所有一级地址，即所有数据块
                    if (insert_new_dir_help(FIRST_BLK + *addr1, new_dir, flag) != 0)
                    { // 失败
                        addr1++;
                        addr1_pos += sizeof(short int);
                    }
                    else
                        return 0;
                }
                // 前面所有一级地址指向的数据块都满了，那么看看是否能新增一级地址
                if (addr1_blk->size < 508)
                { // 可以新增地址, 并增加一个数据块
                    *addr1 = enlarge_blk();
                    if (*addr1 != -1)
                    {
                        addr1_blk->size += sizeof(short int);
                        if (write_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                            return -1;
                        if (insert_new_dir_help(FIRST_BLK + *addr1, new_dir, flag) == -1)
                            return -1;
                        else
                            return 0;
                    }
                }
                addr2++;
                addr2_pos += sizeof(short int);
            }
            // 能否新增二级地址
            if (addr2_blk->size < 508)
            {
                // addr2写入addr[5]指向的二级地址块，addr1 写入addr2指向的地址块，新目录项写入addr1指向的数据块
                *addr2 = enlarge_blk();
                if (*addr2 != -1)
                {
                    addr2_blk->size += sizeof(short int);
                    if (write_data_block(FIRST_BLK + par_inode->addr[i], addr2_blk) == -1)
                        return -1;
                    struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                        return -1;
                    short int *addr1 = (short int *)addr1_blk->data;
                    *addr1 = enlarge_blk();
                    if (*addr1 != -1)
                    {
                        addr1_blk->size += sizeof(short int);
                        if (write_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                            return -1;
                        if (insert_new_dir_help(FIRST_BLK + *addr1, new_dir, flag) == -1)
                            return -1;
                        else
                            return 0;
                    }
                }
            }
        }
        else
        {
            if (par_inode->addr[i] == -1)
            {                                       // 建立三级间址
                par_inode->addr[i] = enlarge_blk(); // 新增三级地址块
                if (par_inode->addr[i] != -1)
                {
                    struct data_block *new_addr3_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + par_inode->addr[i], new_addr3_blk) == -1)
                        return -1;
                    short int *addr3 = (short int *)new_addr3_blk->data;
                    *addr3 = enlarge_blk(); // 新增二级地址块
                    new_addr3_blk->size += sizeof(short int);
                    if (write_data_block(FIRST_BLK + par_inode->addr[i], new_addr3_blk) == -1)
                        return -1;
                    if (*addr3 != -1)
                    {
                        struct data_block *new_addr2_blk = malloc(sizeof(struct data_block));
                        if (read_cpy_data_block(FIRST_BLK + *addr3, new_addr2_blk) == -1)
                            return -1;
                        short int *addr2 = (short int *)new_addr2_blk->data;
                        *addr2 = enlarge_blk(); // 新增一级地址块
                        new_addr2_blk->size += sizeof(short int);
                        if (write_data_block(FIRST_BLK + *addr3, new_addr2_blk) == -1)
                            return -1;
                        if (*addr2 != -1)
                        {
                            struct data_block *new_addr1_blk = malloc(sizeof(struct data_block));
                            if (read_cpy_data_block(FIRST_BLK + *addr2, new_addr1_blk) == -1)
                                return -1;
                            short int *addr1 = (short int *)new_addr1_blk->data;
                            *addr1 = enlarge_blk(); // 新增一级地址块
                            new_addr1_blk->size += sizeof(short int);
                            if (write_data_block(FIRST_BLK + *addr2, new_addr1_blk) == -1)
                                return -1;
                            if (*addr1 != -1 && insert_new_dir_help(FIRST_BLK + *addr1, new_dir, flag) == 0)
                                return 0;
                            else
                                return -1;
                        }
                    }
                }
                else
                {
                    printf("新增三级地址块失败\n\n");
                    return -1; // 新增地址块失败
                }
            }
            // 如果已经有三级间址块
            struct data_block *addr3_blk = malloc(sizeof(struct data_block));
            read_cpy_data_block(FIRST_BLK + par_inode->addr[i], addr3_blk);
            short int *addr3 = (short int *)addr3_blk->data;
            int addr3_pos = 0;
            while (addr3_pos < addr3_blk->size)
            {
                struct data_block *addr2_blk = malloc(sizeof(struct data_block));
                read_cpy_data_block(FIRST_BLK + *addr3, addr2_blk);
                short int *addr2 = (short int *)addr2_blk->data;
                int addr2_pos = 0;
                while (addr2_pos < addr2_blk->size)
                { // 遍历所有二级地址，即所有一级地址块
                    struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                    read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk);
                    short int *addr1 = (short int *)addr1_blk->data;
                    int addr1_pos = 0;
                    while (addr1_pos < addr1_blk->size)
                    { // 遍历所有一级地址，即所有数据块
                        if (insert_new_dir_help(FIRST_BLK + *addr1, new_dir, flag) != 0)
                        { // 失败
                            addr1++;
                            addr1_pos += sizeof(short int);
                        }
                        else
                            return 0;
                    }
                    // 前面所有一级地址指向的数据块都满了，那么看看是否能新增一级地址
                    if (addr1_blk->size < 508)
                    { // 可以新增地址, 并增加一个数据块
                        *addr1 = enlarge_blk();
                        if (*addr1 != -1)
                        {
                            addr1_blk->size += sizeof(short int);
                            if (write_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                                return -1;
                            if (insert_new_dir_help(FIRST_BLK + *addr1, new_dir, flag) == -1)
                                return -1;
                            else
                                return 0;
                        }
                    }
                    addr2++;
                    addr2_pos += sizeof(short int);
                }
                // 前面所有二级地址指向的数据块都满了，那么看看是否能新增二级地址
                if (addr2_blk->size < 508)
                { // 可以新增地址, 并增加一个一级地址块
                    *addr2 = enlarge_blk();
                    if (*addr2 != -1)
                    {
                        addr2_blk->size += sizeof(short int);
                        if (write_data_block(FIRST_BLK + *addr3, addr2_blk) == -1)
                            return -1;
                        struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                        read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk);
                        short int *addr1 = (short int *)addr1_blk->data;
                        *addr1 = enlarge_blk();
                        if (*addr1 != -1)
                        {
                            addr1_blk->size += sizeof(short int);
                            if (write_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                                return -1;
                            if (insert_new_dir_help(FIRST_BLK + *addr1, new_dir, flag) == -1)
                                return -1;
                            else
                                return 0;
                        }
                    }
                }
                addr3++;
                addr3_pos += sizeof(short int);
            }
            // 能否新增三级地址
            if (addr3_blk->size < 508)
            {
                *addr3 = enlarge_blk();
                // addr3写入addr[6]指向的三级地址块， addr2写入addr3指向的二级地址块，addr1 写入addr2指向的地址块，新目录项写入addr1指向的数据块
                if (*addr3 != -1)
                {
                    addr3_blk->size += sizeof(short int);
                    if (write_data_block(FIRST_BLK + par_inode->addr[i], addr3_blk) == -1)
                        return -1;
                    struct data_block *addr2_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + *addr3, addr2_blk) == -1)
                        return -1;
                    short int *addr2 = (short int *)addr2_blk->data;
                    *addr2 = enlarge_blk();
                    if (*addr2 != -1)
                    {
                        addr2_blk->size += sizeof(short int);
                        if (write_data_block(FIRST_BLK + *addr3, addr2_blk) == -1)
                            return -1;
                        struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                        if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                            return -1;
                        short int *addr1 = (short int *)addr1_blk->data;
                        *addr1 = enlarge_blk();
                        if (*addr1 != -1)
                        {
                            addr1_blk->size += sizeof(short int);
                            if (write_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                                return -1;
                            if (insert_new_dir_help(FIRST_BLK + *addr1, new_dir, flag) == -1)
                                return -1;
                            else
                                return 0;
                        }
                    }
                }
            }
        }
    }
    return -1;
}
// 根据文件的块号，从磁盘（5M大文件）中读取数据
// 步骤：① 打开文件；② 将FILE指针移动到文件的相应位置；③ 读出数据块
int read_cpy_data_block(long blk_no, struct data_block *data_blk)
{
    FILE *fp = NULL;
    fp = fopen(disk_path, "r+");
    if (fp == NULL)
    {
        printf("错误：read_cpy_data_block：打开文件失败\n\n");
        return -1;
    }
    // 文件打开成功以后，就用blk_no * FS_BLOCK_SIZE作为偏移量
    fseek(fp, blk_no * FS_BLOCK_SIZE, SEEK_SET);
    fread(data_blk, sizeof(struct data_block), 1, fp);
    if (ferror(fp))
    { // 看读取有没有出错
        printf("错误：read_cpy_data_block：读取文件失败\n\n");
        return -1;
    }
    fclose(fp);
    return 0; // 读取成功则关闭文件，然后返回0
}
// 根据文件的块号，将data_block结构写入到相应的磁盘块里面
// 步骤：① 打开文件；② 将FILE指针移动到文件的相应位置；③写入数据块
int write_data_block(long blk_no, struct data_block *data_blk)
{
    FILE *fp = NULL;
    fp = fopen(disk_path, "r+");
    if (fp == NULL)
    {
        printf("错误：write_data_block：打开文件失败\n\n");
        return -1;
    }
    fseek(fp, blk_no * FS_BLOCK_SIZE, SEEK_SET);
    fwrite(data_blk, sizeof(struct data_block), 1, fp);
    if (ferror(fp))
    { // 看读取有没有出错
        printf("错误：write_data_block：读取文件失败\n\n");
        return -1;
    }
    fclose(fp);
    return 0; // 写入成功则关闭文件，然后返回0
}
// 增加一个新的块, 返回块号（此号+518才是真正的号）
int enlarge_blk()
{
    printf("enlarge_blk：函数开始\n\n");
    struct data_block *data_blk_bitmap = malloc(sizeof(struct data_block));
    int pos = FIRST_BLK_OF_DATABITMAP;
    while (pos < FIRST_INODE) // 遍历四个数据块位图
    {
        if (read_cpy_data_block(pos, data_blk_bitmap) == -1)
            return -1;
        int *bitmap_32 = (int *)data_blk_bitmap;
        int data_ino = 31, mask = 1; // 因为mask=1时数据块号就是31, data_ino是当前32bit中的号
        int count = 0;               // count说明遍历了多少个32bit
        int new_data_ino;            // 真正的空闲bit
        while (1)
        { // 遍历一个数据块
            printf("check 1   mask = %d , *bitmap_32 = %d, mask & *bitmap_32 = %d\n\n", mask, *bitmap_32, mask & *bitmap_32);
            while ((mask & *bitmap_32) != 0x0 && data_ino != 0) //	遍历一个32bit
            {
                mask <<= 1;
                data_ino--;
                printf("check 2   mask = %d , *bitmap_32 = %d, mask & *bitmap_32 = %d", mask, *bitmap_32, mask & *bitmap_32);
            }
            if ((mask & *bitmap_32) == 0x0)
            { // 找到空闲bit
                new_data_ino = count * 32 + data_ino;
                *bitmap_32 |= mask;
                write_data_block(pos, data_blk_bitmap);
                printf("找到可以分配的数据块， 数据块号为: %d\n\n", new_data_ino);
                return new_data_ino;
            }
            else if (count == 127)
                break;
            else if (pos == FIRST_INODE - 1 && count == 127)
            { // 四个块都没有找到
                printf("没有数据块可以分配了\n\n");
                return -1;
            }
            else
            { // 需要下一个32bit查找
                count++;
                data_ino = 31;
                mask = 1;
                bitmap_32++;
            }
        }
        pos++;
    }
    free(data_blk_bitmap);
    return -1;
}
// 释放这个块，并修改位图
int free_blk(short int blk_no)
{
    printf("free_blk函数开始 blk_no=%d\n\n", blk_no);
    struct data_block *data_blk = malloc(sizeof(struct data_block));
    if (read_cpy_data_block(blk_no, data_blk) == -1)
        return -1;
    strcpy(data_blk->data, "\0");
    data_blk->size = 0;
    if (write_data_block(blk_no, data_blk) == -1)
        return -1;
    free(data_blk);
    // 位图
    struct data_block *data_blk_bitmap = malloc(sizeof(struct data_block));
    short int bitmap_blk_no = FIRST_BLK_OF_DATABITMAP;
    bitmap_blk_no += blk_no / FS_BLOCK_SIZE;
    printf("bitmap_blk_no = %d\n\n", bitmap_blk_no);
    if (read_cpy_data_block(bitmap_blk_no, data_blk_bitmap) == -1)
        return -1;
    int *bit_32 = (int *)data_blk_bitmap;
    int t1 = blk_no / 32, t2 = blk_no % 32;
    printf("t1 = %d, t2 = %d\n\n", t1, t2);
    for (int i = 0; i < t1; i++)
        bit_32++;
    int mask = 1;
    mask <<= (31 - t2);
    printf("mask = 0x%x,  bit_32 = 0x%x\n\n", mask, *bit_32);
    mask = ~mask;
    *bit_32 = *bit_32 & mask;
    printf("mask = 0x%x,  bit_32 = 0x%x\n\n", mask, *bit_32);
    if (write_data_block(bitmap_blk_no, data_blk_bitmap) == -1)
        return -1;
    free(data_blk_bitmap);
    return 0;
}
// 删除inode，修改位图
int free_inode(int st_ino)
{
    printf("free_inode函数开始 st_ino=%d\n\n", st_ino);
    struct data_block *bitmap = malloc(sizeof(struct data_block));
    if (read_cpy_data_block(FIRST_BLK_OF_INODEBITMAP, bitmap) == -1)
        return -1;
    int *bit_32 = (int *)bitmap;
    int t1 = st_ino / 32, t2 = st_ino % 32;
    for (int i = 0; i < t1; i++)
        bit_32++;
    int mask = 1;
    mask <<= (31 - t2);
    printf("mask = 0x%x,  bit_32 = 0x%x\n\n", mask, *bit_32);
    mask = ~mask;
    *bit_32 = *bit_32 & mask;
    printf("mask = 0x%x,  bit_32 = 0x%x\n\n", mask, *bit_32);
    if (write_data_block(FIRST_BLK_OF_INODEBITMAP, bitmap) == -1)
        return -1;
    free(bitmap);
    return 0;
}
// 判断该path中是否含有目录和文件，如果为空则返回1，不为空则返回0
int path_is_emp(const char *path)
{
    printf("path_is_emp：函数开始\n\n");
    struct data_block *data_blk = malloc(sizeof(struct data_block));
    struct file_directory *attr = malloc(sizeof(struct file_directory));
    // 读取属性到attr里
    if (get_fd_to_attr(path, attr) == -1)
    {
        printf("错误：path_is_emp：get_fd_to_attr失败，path所指对象的file_directory不存在，函数结束返回\n\n");
        free(data_blk);
        free(attr);
        return 0;
    }
    struct inode *temp_inode = malloc(sizeof(struct inode));
    get_inode(attr->st_ino, temp_inode);
    // 读出块信息,  这里只读了一个块，后面需要读多个块
    // if (read_cpy_data_block(FIRST_BLK + temp_inode->addr[0], data_blk) == -1)
    // {
    // 	printf("错误：path_is_emp：read_cpy_data_block读取块信息失败,函数结束返回\n\n");
    // 	free(data_blk);	free(attr);	return 0;
    // }
    // 	printf("path_is_emp：判断完毕，函数结束返回\n\n");
    // if(data_blk->size == 0) return 1;
    // printf("path_is_emp：判断完毕，检测到该目录下有文件，目录不为空，函数结束返回\n\n");
    // free(data_blk);	free(attr);	return 0;

    // 这是遍历所有块检查
    if (check_inode_emp(temp_inode) == -1)
    {
        printf("path_is_emp: 目录不为空或其他错误\n\n");
        free(data_blk);
        free(attr);
        return 0;
    }
    free(data_blk);
    free(attr);
    return 1;
}
// 遍历块，找到目录项, 并删除， 把这个块的最后一个目录项放到这个空位，保证连续存储
int foreach_datablk_remove_dir(short int blk_no, struct file_directory *dir)
{
    printf("foreach_datablk_remove_dir开始\n\n");
    struct data_block *data_blk = malloc(sizeof(struct data_block));
    struct file_directory *temp_dir;
    if (read_cpy_data_block(blk_no, data_blk) == -1)
        return -1;
    temp_dir = (struct file_directory *)data_blk->data;
    int pos = 0;
    printf("data_blk->size=%d", data_blk->size);
    while (pos < data_blk->size)
    {
        printf("temp_dir->fname=%s, dir->fname=%s\n\n", temp_dir->fname, dir->fname);
        printf("temp_dir->fext=%s, dir->fext=%s\n\n", temp_dir->fext, dir->fext);
        if (strcmp(temp_dir->fname, dir->fname) == 0 && strcmp(temp_dir->fext, dir->fext) == 0)
        {
            // 找到目录项，进行删除, 并替换（实际上就是替换）. 如果这是最后一个，则直接删除即可
            if (pos < data_blk->size - sizeof(struct file_directory))
            {
                printf("不是最后一个目录项\n");
                printf("pos=%d, data_blk->size=%d\n\n", pos, data_blk->size);
                struct file_directory *last_dir = temp_dir;
                while (pos + sizeof(struct file_directory) < data_blk->size)
                {
                    last_dir++;
                    pos += sizeof(struct file_directory);
                }
                printf("temp_dir->fname=%s, last_dir->fname=%s\n\n", temp_dir->fname, last_dir->fname);
                strcpy(temp_dir->fname, last_dir->fname);
                strcpy(temp_dir->fext, last_dir->fext);
                temp_dir->st_ino = last_dir->st_ino;
                strcpy(temp_dir->reserve, last_dir->reserve);
                // 删除最后一个
                strcpy(last_dir->fname, "\0");
                strcpy(last_dir->fext, "\0");
                last_dir->st_ino = -1;
                strcpy(last_dir->reserve, "\0");
            }
            else
            {
                printf("是最后一个目录项\n");
                strcpy(temp_dir->fname, "\0");
                strcpy(temp_dir->fext, "\0");
                temp_dir->st_ino = -1;
                strcpy(temp_dir->reserve, "\0");
            }
            data_blk->size -= sizeof(struct file_directory);
            if (write_data_block(blk_no, data_blk) == -1)
                return -1;
            if (data_blk->size == 0)
                return 1; // 需要释放此数据块
            else
                return 0;
        }
        temp_dir++;
        pos += sizeof(struct file_directory);
        printf("pos=%d, sizeof(struct file_directory)=%d\n\n", pos, sizeof(struct file_directory));
    }
    return -1;
}
// 删除在父目录中的目录项
int remove_dir(struct inode *par_inode, struct file_directory *dir)
{ // dir是要删除的目录项
    // 删除在父目录中的dir
    printf("remove_dir开始 par_inode->st_ino:%d\n\n", par_inode->st_ino);
    for (int i = 0; i < 7; i++)
    {
        printf("当前i = %d    par_inode->addr[i] = %d\n\n", i, par_inode->addr[i]);
        if (i < 4)
        {
            if (par_inode->addr[i] == -1)
                return -1; // 找不到文件
            int f = foreach_datablk_remove_dir(FIRST_BLK + par_inode->addr[i], dir);
            if (f == 0 || i == 0)
            { // 第一块(i==0)不能释放
                printf("remove_dir结束返回\n\n");
                return 0;
            }
            else if (f == 1)
            {
                printf("释放数据块\n\n");
                if (free_blk(par_inode->addr[i]) == -1)
                    return -1;
                par_inode->addr[i] = -1;
                return 0;
            }
        }
        else if (i == 4)
        {
            if (par_inode->addr[i] == -1)
                return -1; // 找不到文件
            struct data_block *addr1_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + par_inode->addr[i], addr1_blk) == -1)
                return -1; // 一级地址块
            short int *addr1 = addr1_blk->data;
            int pos = 0;
            while (pos < addr1_blk->size)
            {
                int f = foreach_datablk_remove_dir(FIRST_BLK + *addr1, dir);
                if (f == 0)
                    return 0;
                else if (f == 1)
                {
                    if (free_blk(*addr1) == -1)
                        return -1;
                    *addr1 = -1;
                    addr1_blk->size -= sizeof(short int);
                    if (write_data_block(FIRST_BLK + par_inode->addr[i], addr1_blk) == -1)
                        return -1;
                    return 0;
                }
                pos += sizeof(short int);
                addr1++;
            }
        }
        else if (i == 5)
        {
            if (par_inode->addr[i] == -1)
                return -1; // 找不到文件
            struct data_block *addr2_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + par_inode->addr[i], addr2_blk) == -1)
                return -1; // 二级地址块
            short int *addr2 = addr2_blk->data;
            int pos2 = 0;
            while (pos2 < addr2_blk->size)
            {
                struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                    return -1; // 一级地址块
                short int *addr1 = addr1_blk->data;
                int pos = 0;
                while (pos < addr1_blk->size)
                {
                    int f = foreach_datablk_remove_dir(FIRST_BLK + *addr1, dir);
                    if (f == 0)
                        return 0;
                    else if (f == 1)
                    {
                        if (free_blk(*addr1) == -1)
                            return -1;
                        *addr1 = -1;
                        addr1_blk->size -= sizeof(short int);
                        if (write_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                            return -1;
                        return 0;
                    }
                    pos += sizeof(short int);
                    addr1++;
                }
                pos2 += sizeof(short int);
                addr2++;
            }
        }
        else
        {
            if (par_inode->addr[i] == -1)
                return -1;
            struct data_block *addr3_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + par_inode->addr[i], addr3_blk) == -1)
                return -1; // 三级地址块
            short int *addr3 = addr3_blk->data;
            int pos3 = 0;
            while (pos3 < addr3_blk->size)
            {
                struct data_block *addr2_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr3, addr2_blk) == -1)
                    return -1; // 二级地址块
                short int *addr2 = addr2_blk->data;
                int pos2 = 0;
                while (pos2 < addr2_blk->size)
                {
                    struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                        return -1; // 一级地址块
                    short int *addr1 = addr1_blk->data;
                    int pos = 0;
                    while (pos < addr1_blk->size)
                    {
                        int f = foreach_datablk_remove_dir(FIRST_BLK + *addr1, dir);
                        if (f == 0)
                            return 0;
                        else if (f == 1)
                        {
                            if (free_blk(*addr1) == -1)
                                return -1;
                            *addr1 = -1;
                            addr1_blk->size -= sizeof(short int);
                            if (write_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                                return -1;
                            return 0;
                        }
                        pos += sizeof(short int);
                        addr1++;
                    }
                    pos2 += sizeof(short int);
                    addr2++;
                }
                pos3 += sizeof(short int);
                addr3++;
            }
        }
    }
    return -1;
}
// 删除此inode对应的所有数据
int remove_inode(struct inode *this)
{
    // 修改位图!!!!!!!!!!!!!!!!
    printf("remove_inode开始\n\n");
    for (int i = 0; i < 7; i++)
    {
        printf("当前i = %d    this->addr[i] = %d\n\n", i, this->addr[i]);
        if (this->addr[i] == -1)
            return 0; // 删除完成
        if (i < 4)
        {
            if (free_blk(this->addr[i]) == -1)
                return -1;
        }
        else if (i == 4)
        {
            struct data_block *addr1_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + this->addr[i], addr1_blk) == -1)
                return -1;
            short int *addr1 = addr1_blk->data;
            int pos = 0;
            while (pos < addr1_blk->size)
            {
                if (free_blk(FIRST_BLK + *addr1) == -1)
                    return -1;
                pos += sizeof(short int);
                addr1++;
            }
            if (free_blk(FIRST_BLK + this->addr[i]) == -1)
                return -1;
        }
        else if (i == 5)
        {
            struct data_block *addr2_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + this->addr[i], addr2_blk) == -1)
                return -1;
            short int *addr2 = addr2_blk->data;
            int pos2 = 0;
            while (pos2 < addr2_blk->size)
            {
                struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                    return -1;
                short int *addr1 = addr1_blk->data;
                int pos = 0;
                while (pos < addr1_blk->size)
                {
                    if (free_blk(FIRST_BLK + *addr1) == -1)
                        return -1;
                    pos += sizeof(short int);
                    addr1++;
                }
                if (free_blk(FIRST_BLK + *addr2) == -1)
                    return -1;
                pos2 += sizeof(short int);
                addr2++;
            }
            if (free_blk(FIRST_BLK + this->addr[i]) == -1)
                return -1;
        }
        else
        {
            struct data_block *addr3_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + this->addr[i], addr3_blk) == -1)
                return -1;
            short int *addr3 = addr3_blk->data;
            int pos3 = 0;
            while (pos3 < addr3_blk->size)
            {
                struct data_block *addr2_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr3, addr2_blk) == -1)
                    return -1;
                short int *addr2 = addr2_blk->data;
                int pos2 = 0;
                while (pos2 < addr2_blk->size)
                {
                    struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                        return -1;
                    short int *addr1 = addr1_blk->data;
                    int pos = 0;
                    while (pos < addr1_blk->size)
                    {
                        if (free_blk(FIRST_BLK + *addr1) == -1)
                            return -1;
                        pos += sizeof(short int);
                        addr1++;
                    }
                    if (free_blk(FIRST_BLK + *addr2) == -1)
                        return -1;
                    pos2 += sizeof(short int);
                    addr2++;
                }
                if (free_blk(FIRST_BLK + *addr3) == -1)
                    return -1;
                pos3 += sizeof(short int);
                addr3++;
            }
            if (free_blk(FIRST_BLK + this->addr[i]) == -1)
                return -1;
        }
    }
    return 0;
}
// 检查此inode的addr对应的所有数据块是否为空
int check_inode_emp(struct inode *this)
{
    printf("check_inode_emp开始\n\n");
    for (int i = 0; i < 7; i++)
    {
        printf("当前i = %d    this->addr[i] = %d\n\n", i, this->addr[i]);
        if (this->addr[i] == -1)
            return 0;
        if (i < 4)
        {
            struct data_block *data_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + this->addr[i], data_blk) == -1)
                return -1;
            if (data_blk->size != 0)
                return -1;
        }
        else if (i == 4)
        {
            struct data_block *addr1_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + this->addr[i], addr1_blk) == -1)
                return -1;
            short int *addr1 = addr1_blk->data;
            int pos = 0;
            while (pos < addr1_blk->size)
            {
                struct data_block *data_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr1, data_blk) == -1)
                    return -1;
                if (data_blk->size != 0)
                    return -1;
                pos += sizeof(short int);
                addr1++;
            }
        }
        else if (i == 5)
        {
            struct data_block *addr2_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + this->addr[i], addr2_blk) == -1)
                return -1;
            short int *addr2 = addr2_blk->data;
            int pos2 = 0;
            while (pos2 < addr2_blk->size)
            {
                struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                    return -1;
                short int *addr1 = addr1_blk->data;
                int pos = 0;
                while (pos < addr1_blk->size)
                {
                    struct data_block *data_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + *addr1, data_blk) == -1)
                        return -1;
                    if (data_blk->size != 0)
                        return -1;
                    pos += sizeof(short int);
                    addr1++;
                }
                pos2 += sizeof(short int);
                addr2++;
            }
        }
        else
        {
            struct data_block *addr3_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + this->addr[i], addr3_blk) == -1)
                return -1;
            short int *addr3 = addr3_blk->data;
            int pos3 = 0;
            while (pos3 < addr3_blk->size)
            {
                struct data_block *addr2_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr3, addr2_blk) == -1)
                    return -1;
                short int *addr2 = addr2_blk->data;
                int pos2 = 0;
                while (pos2 < addr2_blk->size)
                {
                    struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                        return -1;
                    short int *addr1 = addr1_blk->data;
                    int pos = 0;
                    while (pos < addr1_blk->size)
                    {
                        struct data_block *data_blk = malloc(sizeof(struct data_block));
                        if (read_cpy_data_block(FIRST_BLK + *addr1, data_blk) == -1)
                            return -1;
                        if (data_blk->size != 0)
                            return -1;
                        pos += sizeof(short int);
                        addr1++;
                    }
                    pos2 += sizeof(short int);
                    addr2++;
                }
                pos3 += sizeof(short int);
                addr3++;
            }
        }
    }
    return 0;
}
// 在这个块中读取数据, 返回值在父函数中赋值给flag，3表示读取完毕
int read_on_blk(short int blk_no, int flag, char *buf, off_t *offset, int *temp_size, size_t size)
{
    struct data_block *data_blk = malloc(sizeof(struct data_block));
    if (read_cpy_data_block(blk_no, data_blk) == -1)
        return -1;
    if (flag == 1)
    {
        char *pt = data_blk->data;
        pt += *offset;
        if (*offset + size > data_blk->size)
        { // 要读的数据比这个块还多
            strncpy(buf, pt, data_blk->size - *offset);
            *temp_size -= (data_blk->size - *offset);
            return 2;
        }
        else
        {
            strncpy(buf, pt, size);
            *temp_size = 0;
            return 3;
        }
    }
    else if (flag == 2)
    {
        if (*temp_size > data_blk->size)
        {
            memcpy(buf + size - *temp_size, data_blk->data, data_blk->size); // 从buf上一次结束的位置开始继续记录数据
            *temp_size -= data_blk->size;
            return 2;
        }
        else
        {
            memcpy(buf + size - *temp_size, data_blk->data, *temp_size); // 从buf上一次结束的位置开始继续记录数据
            *temp_size = 0;
            return 3;
        }
    }
    else
    {
        if (*offset > data_blk->size)
        {
            *offset -= data_blk->size; // 跳过这个块
            return 0;
        }
        // else return 1;
        else
        {
            flag = 1;
            read_on_blk(blk_no, flag, buf, offset, temp_size, size);
        }
    }
}
// 在这个块中写入数据, 返回值在父函数中赋值给flag，3表示写入完毕
int write_on_blk(short int blk_no, int flag, char *buf, off_t *offset, int *temp_size, size_t size)
{
    printf("write_on_blk函数: blk_no=%d, flag=%d, buf=%s, offset=%d, temp_size=%d, size=%d\n\n", blk_no, flag, buf, *offset, *temp_size, size);
    struct data_block *data_blk = malloc(sizeof(struct data_block));
    if (read_cpy_data_block(blk_no, data_blk) == -1)
        return -1;
    if (flag == 1)
    {
        if (data_blk->size < *offset)
            return -2; // 越界了
        char *pt = data_blk->data;
        pt += *offset;
        if (*offset + size > 508)
        { // 要写的数据比这个块还多
            printf("块内不够写\n");
            strncpy(pt, buf, 508 - *offset);
            *temp_size -= (508 - *offset);
            data_blk->size = 508;
            if (write_data_block(blk_no, data_blk) == -1)
                return -1;
            printf("write_on_blk结束, data_blk->data: %s\n", data_blk->data);
            return 2;
        }
        else
        {
            printf("块内够写\n");
            strncpy(pt, buf, size);
            *temp_size = 0;
            data_blk->size = *offset + size;
            if (write_data_block(blk_no, data_blk) == -1)
                return -1;
            printf("write_on_blk结束, data_blk->data: %s\n", data_blk->data);
            return 3;
        }
    }
    else if (flag == 2)
    {
        if (*temp_size > 508)
        {
            printf("块内不够写\n");
            memcpy(data_blk->data, buf + size - *temp_size, 508); // 从buf上一次结束的位置开始继续记录数据
            *temp_size -= 508;
            data_blk->size = 508;
            if (write_data_block(blk_no, data_blk) == -1)
                return -1;
            printf("write_on_blk结束, data_blk->data: %s\n", data_blk->data);
            return 2;
        }
        else
        {
            printf("块内够写\n");
            memcpy(data_blk->data, buf + size - *temp_size, *temp_size); // 从buf上一次结束的位置开始继续记录数据
            data_blk->size = *temp_size;
            *temp_size = 0;
            if (write_data_block(blk_no, data_blk) == -1)
                return -1;
            printf("write_on_blk结束, data_blk->data: %s\n", data_blk->data);
            return 3;
        }
    }
    else
    {
        if (*offset > 508)
        {
            *offset -= 508; // 跳过这个块
            return 0;
        }
        flag = 1;
        write_on_blk(blk_no, flag, buf, offset, temp_size, size);
    }
}
/***************************************************************************************************************************/
// 三个功能函数:getattr,create_file_dir,remove_file_dir
// 根据文件的路径，到相应的目录寻找该文件的file_directory，并赋值给attr
int get_fd_to_attr(const char *path, struct file_directory *attr)
{
    printf("get_fd_to_attr要查找的文件路径：%s \n\n", path);
    // 先要读取超级块，获取磁盘根目录块的位置
    struct sb *sb_blk;
    struct data_block *data_blk = malloc(sizeof(struct data_block));
    // 把超级块读出来
    if (read_cpy_data_block(0, data_blk) == -1)
    {
        printf("get_sb：读取超级块失败，函数结束返回\n\n");
        free(data_blk);
        return -1;
    }
    sb_blk = (struct sb *)data_blk;

    long start_blk;
    start_blk = sb_blk->first_blk;
    printf("检查sb_blk:\nfs_size=%ld\nfirst_blk=%ld\ndatasize=%ld\nfirst_inode=%ld\n", sb_blk->fs_size, sb_blk->first_blk, sb_blk->datasize, sb_blk->first_inode);
    printf("inode_area_size=%ld\nfirst_blk_of_inodebitmap=%ld\nnodebitmap_size=%ld\n", sb_blk->inode_area_size, sb_blk->first_blk_of_inodebitmap, sb_blk->inodebitmap_size);
    printf("first_blk_of_databitmap=%ld\ndatabitmap_size=%ld\n\n", sb_blk->first_blk_of_databitmap, sb_blk->databitmap_size);
    printf("start_blk:%ld\n\n", start_blk);
    // 如果路径为空，则出错返回1
    if (!path)
    {
        printf("错误：get_fd_to_attr：路径为空，函数结束返回\n\n");
        free(sb_blk);
        return -1;
    }
    // char *test = strchr(path,'.'); //暂时这样，创建成功再说
    // if(test!=NULL){
    // 	printf("初始化文件夹\n\n");
    // 	free(sb_blk);
    // 	return -1;
    // }
    struct inode *temp_inode = malloc(sizeof(struct inode));
    struct file_directory *temp_dir = malloc(sizeof(struct file_directory));
    if (get_inode_and_dir_bypath(path, temp_inode, temp_dir) == -1)
        return -1;
    strcpy(attr->fname, temp_dir->fname);
    strcpy(attr->fext, temp_dir->fext);
    strcpy(attr->reserve, temp_dir->reserve);
    attr->st_ino = temp_dir->st_ino;
    free(temp_inode);
    free(temp_dir);
    return 0;
}
// 创建path所指的文件或目录的file_directory，并为该文件（目录）申请空闲块，创建成功返回0，创建失败返回-1
// mkdir和mknod这两种操作都要用到
int create_file_dir(const char *path, int flag) // 1表示文件，2表示目录
{
    printf("调用了create_file_dir，创建的类型是：%d，创建的路径是：%s\n\n", flag, path);
    // 父目录inode，要创建的文件名 -> inode位图 -> 得到inode并修改属性，写回inode区 -> 插入新目录项 -> 写回父目录inode
    char *temp_path = strdup(path);
    char *p = temp_path;        // p是最后一个/, 下面做完后变成要创建的文件名
    char *par_path = temp_path; // 父目录
    // /home
    if (strchr(++temp_path, '/') == NULL)
    {
        par_path = "/";
        p = temp_path;
    }
    else
    { // /home/lwd
        while (temp_path != NULL)
        {
            printf("现在的temp_path: %s\n", temp_path);
            p = temp_path;
            temp_path++;
            if (temp_path != NULL)
                temp_path = strchr(temp_path, '/');
        }
        *p = '\0';
        p++; // p是文件名, par是父目录
    }
    printf("父目录：%s, 要创建的文件：%s\n\n", par_path, p);
    if (strlen(p) > 8)
    {
        printf("文件名太长, create_file_dir失败, 函数结束\n");
        return -ENAMETOOLONG; // 文件名太长
    }

    struct inode *temp_inode = malloc(sizeof(struct inode));
    struct file_directory *temp_dir = malloc(sizeof(struct file_directory));
    get_inode_and_dir_bypath(par_path, temp_inode, temp_dir);
    printf("get_inode_and_dir_bypath成功后得到的inode->addr[0]= %d\n\n", temp_inode->addr[0]);
    // inode位图
    printf("check1\n\n");
    struct data_block *data_blk = malloc(sizeof(struct data_block));
    if (read_cpy_data_block(FIRST_BLK_OF_INODEBITMAP, data_blk) == -1)
        return -1;
    int *bitmap_32 = (int *)data_blk;
    int mask = 1;
    int st_ino = 31; // 因为mask=1时就inode号就是31, st_ino是当前32bit中的号
    int count = 0;   // count说明遍历了多少个32bit
    int new_st_ino;  // 真正的空闲bit
    while (1)
    {
        while (((mask & *bitmap_32) != 0x0) && st_ino != 0) // 此bit被占用
        {
            mask <<= 1;
            st_ino--;
            printf("check3 count=%d, st_ino=%d\n", count, st_ino);
            printf("check3 mask=0x%x, *bitmap_32=0x%x  &: %d flag=0x%x\n\n", mask, *bitmap_32, mask & *bitmap_32, mask & *bitmap_32);
        }
        if ((mask & *bitmap_32) == 0x0)
        { // 找到空闲bit
            new_st_ino = count * 32 + st_ino;
            *bitmap_32 |= mask;
            if (write_data_block(FIRST_BLK_OF_INODEBITMAP, data_blk) == -1)
                return -errno;
            printf("找到可以分配的inode， inode号为: %d\n\n", new_st_ino);
            break;
        }
        else if (count == 127)
        { // 找不到, 且最后一块已经遍历完
            printf("没有inode可以分配了\n\n");
            return -errno;
        }
        else
        { // 需要下一个32bit查找
            count++;
            st_ino = 31;
            mask = 1;
            bitmap_32++;
        }
    }
    printf("check4 new_st_ino=%d\n\n", new_st_ino);
    char *ext = strchr(p, '.'); // 帮助得到拓展名
    printf("ext=%s\n", ext);
    if (ext != NULL)
        *ext = '\0'; // 此时p为文件名，ext为拓展名
    struct file_directory *new_dir = malloc(sizeof(struct file_directory));
    strcpy(new_dir->fname, p);
    printf("check4-1\n");
    if (ext != NULL)
        strcpy(new_dir->fext, ++ext);
    else
        strcpy(new_dir->fext, "\0");
    printf("新文件的fname:%s, 拓展名: %s\n\n", new_dir->fname, new_dir->fext);
    new_dir->st_ino = new_st_ino;
    if (insert_new_dir(temp_inode, new_dir, flag) == 0)
        printf("目录项增加成功\n\n");
    else
        return -1;
    // 这里父目录的temp_inode需要重新写回
    printf("check5\n\n");
    FILE *fp = NULL;
    fp = fopen(disk_path, "r+");
    if (fp == NULL)
    {
        fprintf(stderr, "错误：打开文件失败，文件不存在，函数结束返回\n");
        return;
    }
    fseek(fp, FIRST_INODE * FS_BLOCK_SIZE + temp_inode->st_ino * 64, SEEK_SET); // 找到对应的inode
    fwrite(temp_inode, sizeof(struct inode), 1, fp);
    printf("inode地址修改成功\n\n");
    fclose(fp);

    printf("create_file_dir：创建文件成功，函数结束返回\n\n");
    return 0;
}
// 删除path所指的文件或目录的file_directory和文件的数据块，成功返回0，失败返回-1
int remove_file_dir(const char *path, int flag)
{
    printf("remove_file_dir：函数开始\n\n");
    struct file_directory *attr = malloc(sizeof(struct file_directory));
    // 读取文件属性
    if (get_fd_to_attr(path, attr) == -1)
    {
        free(attr);
        printf("错误：remove_file_dir：get_fd_to_attr失败，函数结束返回\n\n");
        return -ENOENT;
    }
    printf("检查attr:fname=%s，fext=%s, st_ino=%ld， reserve=%s\n\n", attr->fname, attr->fext, attr->st_ino, attr->reserve);

    char *temp_path = strdup(path);
    char *p = temp_path;        // p是最后一个/, 下面做完后变成要创建的文件名
    char *par_path = temp_path; // 父目录
    // /home
    if (strchr(++temp_path, '/') == NULL)
    {
        par_path = "/";
        p = temp_path;
    }
    else
    { // /home/lwd
        while (temp_path != NULL)
        {
            printf("现在的temp_path: %s\n", temp_path);
            p = temp_path;
            temp_path++;
            if (temp_path != NULL)
                temp_path = strchr(temp_path, '/');
        }
        *p = '\0';
        p++; // p是文件名, par是父目录
    }
    printf("父目录：%s, 要删除的文件：%s\n\n", par_path, p);

    struct data_block *data_blk = malloc(sizeof(struct data_block));
    struct inode *temp_inode = malloc(sizeof(struct inode));
    struct inode *par_inode = malloc(sizeof(struct inode));
    struct file_directory *par_dir = malloc(sizeof(struct file_directory));
    // get_inode_and_dir_bypath(par_path, temp_inode, par_dir); // par_dir没什么用的}
    get_inode_and_dir_bypath(par_path, par_inode, par_dir); // par_dir没什么用的}
    get_inode(attr->st_ino, temp_inode);
    printf("par_inode->st_ino=%d\n\n", par_inode->st_ino);
    if ((temp_inode->st_mode & S_IFMT) == S_IFDIR)
    {
        printf("要删除的是目录\n");
        if (!path_is_emp(path)) // 只能删除空的目录，非空则返回错误信息
        {
            printf("check\n");
            free(data_blk);
            free(attr);
            free(temp_inode);
            free(par_inode);
            free(par_dir);
            printf("remove_file_dir：要删除的目录不为空，删除失败，函数结束返回\n\n");
            return -ENOTEMPTY;
        }
        // 删除目录文件
        printf("check1\n");
        if (remove_dir(par_inode, attr) == -1)
            return -1; // 删除在父目录中的目录项
        if (remove_inode(temp_inode) == -1)
            return -1; // 删除inode对应的文件，包括数据块位图也要改
        if (free_inode(temp_inode->st_ino) == -1)
            return -1; // 修改inode位图
    }
    else
    {
        printf("要删除的是文件\n");
        // 删除文件
        if (remove_dir(par_inode, attr) == -1)
            return -1; // 删除在父目录中的目录项
        if (remove_inode(temp_inode) == -1)
            return -1; // 删除inode对应的所有数据块，包括数据块位图也要改
        if (free_inode(temp_inode->st_ino) == -1)
            return -1; // 修改inode位图
    }
    printf("remove_file_dir：删除成功，函数结束返回\n\n");
    free(data_blk);
    free(attr);
    free(temp_inode);
    free(par_inode);
    free(par_dir);
    return 0;
}
/***************************************************************************************************************************/
// 文件系统初始化函数，载入文件系统的时候系统需要知道这个文件系统的大小（以块为单位）
static void *SFS_init(struct fuse_conn_info *conn)
{
    printf("SFS_init：函数开始\n\n");
    (void)conn;

    FILE *fp = NULL;
    fp = fopen(disk_path, "r+");
    if (fp == NULL)
    {
        fprintf(stderr, "错误：打开文件失败，文件不存在，函数结束返回\n");
        return;
    }
    // 先读超级块
    struct sb *super_block = malloc(sizeof(struct sb));
    fread(super_block, sizeof(struct sb), 1, fp);
    // 用超级块中的fs_size初始化全局变量
    TOTAL_BLOCK_NUM = super_block->fs_size;
    fclose(fp);
    printf("SFS_init：函数结束返回\n\n");
}
// 头文件的stat结构体定义如下
/*struct stat {
        mode_t     st_mode;       //文件对应的模式，文件，目录等
        ino_t      st_ino;       //inode节点号
        dev_t      st_dev;        //设备号码
        dev_t      st_rdev;       //特殊设备号码
        nlink_t    st_nlink;      //文件的连接数
        uid_t      st_uid;        //文件所有者
        gid_t      st_gid;        //文件所有者对应的组
        off_t      st_size;       //普通文件，对应的文件字节数
        time_t     st_atime;      //文件最后被访问的时间
        time_t     st_mtime;      //文件内容最后被修改的时间
        time_t     st_ctime;      //文件状态改变时间
        blksize_t st_blksize;    //文件内容对应的块大小
        blkcnt_t   st_blocks;     //文件内容对应的块数量
      };*/
// 该函数用于读取文件属性（并赋值给stbuf）
static int SFS_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    //(void) fi;
    int res = 0;
    struct file_directory *attr = malloc(sizeof(struct file_directory));
    // 非根目录
    if (get_fd_to_attr(path, attr) == -1)
    {
        printf("check1\n\n");
        free(attr);
        printf("SFS_getattr：get_fd_to_attr时发生错误，函数结束返回\n\n");
        return -ENOENT;
    }
    printf("check2\n\n");
    memset(stbuf, 0, sizeof(struct stat)); // 将stat结构中成员的值全部置0

    FILE *fp = NULL;
    fp = fopen(disk_path, "r+");
    if (fp == NULL)
    {
        fprintf(stderr, "错误：打开文件失败，文件不存在，函数结束返回\n");
        return;
    }
    struct inode *this_inode = malloc(sizeof(struct inode));
    fseek(fp, FIRST_INODE * FS_BLOCK_SIZE + attr->st_ino * 64, SEEK_SET); // 找到对应的inode
    fread(this_inode, sizeof(struct inode), 1, fp);
    stbuf->st_ino = this_inode->st_ino;
    stbuf->st_atime = this_inode->st_atim.tv_sec; // 秒级时间
    if ((this_inode->st_mode & S_IFMT) == S_IFDIR)
    { // 从path判断这个文件是		一个目录	还是	一般文件
        printf("SFS_getattr：这个file_directory是一个目录, 目录名：%s 分配的inode号为%d\n\n", attr->fname, this_inode->st_ino);
        // stbuf->st_mode = this_inode->st_mode;
        stbuf->st_mode = S_IFDIR | 0666;
    }
    else if ((this_inode->st_mode & S_IFMT) == S_IFREG)
    {
        printf("SFS_getattr：这个file_directory是一个文件\n\n");
        // stbuf->st_mode = this_inode->st_mode;
        stbuf->st_mode = S_IFREG | 0666;
        stbuf->st_size = this_inode->st_size; // 文件大小
    }
    else
    {
        printf("SFS_getattr：这个文件（目录）不存在，函数结束返回\n\n");
        ;
        res = -ENOENT;
    } // 文件不存在

    printf("SFS_getattr：getattr成功，函数结束返回\n\n");
    free(attr);
    free(this_inode);
    return res;
}
// 创建文件
static int SFS_mknod(const char *path, mode_t mode, dev_t dev)
{
    return create_file_dir(path, 1);
}
// 删除文件
static int SFS_unlink(const char *path)
{
    return remove_file_dir(path, 1);
}
// 打开文件
static int SFS_open(const char *path, struct fuse_file_info *fi)
{
    return 0;
}
// 读取文件时的操作
// 根据路径path找到文件起始位置，再偏移offset长度开始读取size大小的数据到buf中，返回文件大小
// 其中，buf用来存储从path读出来的文件信息，size为文件大小，offset为读取时候的偏移量，fi为fuse的文件信息
static int SFS_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    printf("SFS_read：函数开始\n\n");
    struct file_directory *attr = malloc(sizeof(struct file_directory));
    // 读取该path所指对象的file_directory
    if (get_fd_to_attr(path, attr) == -1)
    {
        free(attr);
        printf("错误：SFS_read：get_fd_to_attr失败，函数结束返回\n\n");
        return -ENOENT;
    }
    struct inode *this_inode = malloc(sizeof(struct inode));
    get_inode(attr->st_ino, this_inode);
    // 如果读取到的对象是目录，那么返回错误（只有文件会用到read这个函数）
    if ((this_inode->st_mode & S_IFMT) == S_IFDIR)
    {
        free(attr);
        printf("错误：SFS_read：对象为目录不是文件，读取失败，函数结束返回\n\n");
        return -EISDIR;
    }
    struct data_block *data_blk = malloc(sizeof(struct data_block));
    // 根据文件信息读取文件内容
    int flag = 0;         // 是否已经到了offset的第一个块, 0未到，1到了，2已经把offset的第一个块读好了, 3表示全部读取完毕
    int temp_size = size; // 剩余未读的数量
    for (int i = 0; i < 7; i++)
    {
        printf("当前i = %d  this_inode->addr[i] = %d,  flag = %d\n\n", i, this_inode->addr[i], flag);
        if (flag == 3)
            break; // 读取完毕
        else if (flag == -1)
            return -1;
        if (this_inode->addr[i] == -1)
            break;
        if (i < 4)
            flag = read_on_blk(FIRST_BLK + this_inode->addr[i], flag, buf, &offset, &temp_size, size);
        else if (i == 4)
        {
            struct data_block *addr1_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + this_inode->addr[i], addr1_blk) == -1)
                return -1;
            short int *addr1 = addr1_blk->data;
            int pos1 = 0;
            while (flag != 3 && pos1 < addr1_blk->size)
            {
                flag = read_on_blk(FIRST_BLK + *addr1, flag, buf, &offset, &temp_size, size);
                addr1++;
                pos1 += sizeof(short int);
            }
        }
        else if (i == 5)
        {
            struct data_block *addr2_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + this_inode->addr[i], addr2_blk) == -1)
                return -1;
            short int *addr2 = addr2_blk->data;
            int pos2 = 0;
            while (flag != 3 && pos2 < addr2_blk->size)
            {
                struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                    return -1;
                short int *addr1 = addr1_blk->data;
                int pos1 = 0;
                while (flag != 3 && pos1 < addr1_blk->size)
                {
                    flag = read_on_blk(FIRST_BLK + *addr1, flag, buf, &offset, &temp_size, size);
                    addr1++;
                    pos1 += sizeof(short int);
                }
                addr2++;
                pos2 += sizeof(short int);
            }
        }
        else
        {
            struct data_block *addr3_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + this_inode->addr[i], addr3_blk) == -1)
                return -1;
            short int *addr3 = addr3_blk->data;
            int pos3 = 0;
            while (flag != 3 && pos3 < addr3_blk->size)
            {
                struct data_block *addr2_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr3, addr2_blk) == -1)
                    return -1;
                short int *addr2 = addr2_blk->data;
                int pos2 = 0;
                while (flag != 3 && pos2 < addr2_blk->size)
                {
                    struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                        return -1;
                    short int *addr1 = addr1_blk->data;
                    int pos1 = 0;
                    while (flag != 3 && pos1 < addr1_blk->size)
                    {
                        flag = read_on_blk(FIRST_BLK + *addr1, flag, buf, &offset, &temp_size, size);
                        addr1++;
                        pos1 += sizeof(short int);
                    }
                    addr2++;
                    pos2 += sizeof(short int);
                }
                addr3++;
                pos3 += sizeof(short int);
            }
        }
    }
    printf("SFS_read：文件读取成功，函数结束返回\n\n");
    free(attr);
    free(data_blk);
    // return size;
    return size - temp_size;
}
// 修改文件,将buf里大小为size的内容，写入path指定的起始块后的第offset
static int SFS_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    printf("SFS_write：函数开始\n\n");
    struct file_directory *attr = malloc(sizeof(struct file_directory));
    struct inode *this_inode = malloc(sizeof(struct inode));
    // 打开path所指的对象，将其file_directory读到attr中
    if (get_fd_to_attr(path, attr) == -1)
        return -1;
    get_inode(attr->st_ino, this_inode);
    int flag = 0;         // 是否已经到了offset的第一个块, 0未到，1到了，2已经把offset的第一个块写好了, 3表示buf里面的东西全部写好
    int temp_size = size; // 剩余未写的数量
    int temp_offset = offset;
    for (int i = 0; i < 7; i++)
    {
        printf("当前i = %d  this_inode->addr[i] = %d,  flag = %d\n\n", i, this_inode->addr[i], flag);
        if (flag == 3)
            break; // 写入完毕
        else if (flag == -1)
            return -1; // 出错
        else if (flag == -2)
        {
            printf("SFS_write：offset越界，函数结束返回\n\n");
            return -EFBIG;
        }
        if (i < 4)
        {
            if (this_inode->addr[i] == -1)
            { // 新增块
                this_inode->addr[i] = enlarge_blk();
                if (this_inode->addr[i] == -1)
                {
                    printf("SFS_write：文件还未写完，申请空闲块失败，函数结束返回\n\n");
                    return -errno;
                }
                flag = write_on_blk(FIRST_BLK + this_inode->addr[i], flag, buf, &offset, &temp_size, size);
            }
            else
                flag = write_on_blk(FIRST_BLK + this_inode->addr[i], flag, buf, &offset, &temp_size, size);
        }
        else if (i == 4)
        {
            struct data_block *addr1_blk = malloc(sizeof(struct data_block));
            if (this_inode->addr[i] == -1)
            {
                this_inode->addr[i] = enlarge_blk();
                if (this_inode->addr[i] == -1)
                {
                    printf("SFS_write：文件还未写完，申请一级地址块失败，函数结束返回\n\n");
                    return -errno;
                }
            }
            if (read_cpy_data_block(FIRST_BLK + this_inode->addr[i], addr1_blk) == -1)
                return -1;
            short int *addr1 = addr1_blk->data;
            int pos1 = 0;
            while (flag != 3 && pos1 < addr1_blk->size)
            {
                flag = write_on_blk(FIRST_BLK + *addr1, flag, buf, &offset, &temp_size, size);
                if (flag == -2)
                {
                    printf("SFS_write：offset越界，函数结束返回\n\n");
                    return -EFBIG;
                }
                else if (flag == -1)
                    return -1;
                addr1++;
                pos1 += sizeof(short int);
            }
            while (flag != 3 && pos1 < 508) // 还没写完但是没有此文件的块不够了, 申请数据块
            {
                *addr1 = enlarge_blk();
                if (*addr1 == -1)
                {
                    printf("SFS_write：文件还未写完，申请空闲块失败，函数结束返回\n\n");
                    return -errno;
                }
                flag = write_on_blk(FIRST_BLK + *addr1, flag, buf, &offset, &temp_size, size);
                if (flag == -2)
                {
                    printf("SFS_write：offset越界，函数结束返回\n\n");
                    return -EFBIG;
                }
                else if (flag == -1)
                    return -1;
                addr1_blk->size += sizeof(short int);
                addr1++;
                pos1 += sizeof(short int);
            }
            // 写回一级地址块
            if (write_data_block(FIRST_BLK + this_inode->addr[i], addr1_blk) == -1)
                return -1;
        }
        else if (i == 5)
        {
            struct data_block *addr2_blk = malloc(sizeof(struct data_block));
            if (this_inode->addr[i] == -1)
            {
                this_inode->addr[i] = enlarge_blk();
                if (this_inode->addr[i] == -1)
                {
                    printf("SFS_write：文件还未写完，申请二级地址块失败，函数结束返回\n\n");
                    return -errno;
                }
            }
            if (read_cpy_data_block(FIRST_BLK + this_inode->addr[i], addr2_blk) == -1)
                return -1;
            short int *addr2 = addr2_blk->data;
            int pos2 = 0;
            while (flag != 3 && pos2 < addr2_blk->size)
            {
                struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                    return -1;
                short int *addr1 = addr1_blk->data;
                int pos1 = 0;
                while (flag != 3 && pos1 < addr1_blk->size)
                {
                    flag = write_on_blk(FIRST_BLK + *addr1, flag, buf, &offset, &temp_size, size);
                    if (flag == -2)
                    {
                        printf("SFS_write：offset越界，函数结束返回\n\n");
                        return -EFBIG;
                    }
                    else if (flag == -1)
                        return -1;
                    addr1++;
                    pos1 += sizeof(short int);
                }
                while (flag != 3 && pos1 < 508) // 还没写完但是没有此文件的块不够了, 申请数据块
                {
                    *addr1 = enlarge_blk();
                    if (*addr1 == -1)
                    {
                        printf("SFS_write：文件还未写完，申请空闲块失败，函数结束返回\n\n");
                        return -errno;
                    }
                    flag = write_on_blk(FIRST_BLK + *addr1, flag, buf, &offset, &temp_size, size);
                    if (flag == -2)
                    {
                        printf("SFS_write：offset越界，函数结束返回\n\n");
                        return -EFBIG;
                    }
                    else if (flag == -1)
                        return -1;
                    addr1_blk->size += sizeof(short int);
                    addr1++;
                    pos1 += sizeof(short int);
                }
                // 写回一级地址块
                if (write_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                    return -1;
                addr2++;
                pos2 += sizeof(short int);
            }
            while (flag != 3 && pos2 < 508) // 还没写完但是没有此文件的块不够了, 申请一级地址块
            {
                *addr2 = enlarge_blk();
                if (*addr2 == -1)
                {
                    printf("SFS_write：文件还未写完，申请一级地址块失败，函数结束返回\n\n");
                    return -errno;
                }
                struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                    return -1;
                short int *addr1 = addr1_blk->data;
                int pos1 = 0;
                while (flag != 3 && pos1 < 508)
                {
                    *addr1 = enlarge_blk();
                    if (*addr1 == -1)
                    {
                        printf("SFS_write：文件还未写完，申请空闲块失败，函数结束返回\n\n");
                        return -errno;
                    }
                    flag = write_on_blk(FIRST_BLK + *addr1, flag, buf, &offset, &temp_size, size);
                    if (flag == -2)
                    {
                        printf("SFS_write：offset越界，函数结束返回\n\n");
                        return -EFBIG;
                    }
                    else if (flag == -1)
                        return -1;
                    addr1_blk->size += sizeof(short int);
                    addr1++;
                    pos1 += sizeof(short int);
                }
                // 写回一级地址块
                if (write_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                    return -1;
                addr2_blk->size += sizeof(short int);
                addr2++;
                pos2 += sizeof(short int);
            }
            // 写回二级地址块
            if (write_data_block(FIRST_BLK + this_inode->addr[i], addr2_blk) == -1)
                return -1;
        }
        else
        {
            struct data_block *addr3_blk = malloc(sizeof(struct data_block));
            if (this_inode->addr[i] == -1)
            {
                this_inode->addr[i] = enlarge_blk();
                if (this_inode->addr[i] == -1)
                {
                    printf("SFS_write：文件还未写完，申请三级地址块失败，函数结束返回\n\n");
                    return -errno;
                }
            }
            if (read_cpy_data_block(FIRST_BLK + this_inode->addr[i], addr3_blk) == -1)
                return -1;
            short int *addr3 = addr3_blk->data;
            int pos3 = 0;
            while (flag != 3 && pos3 < addr3_blk->size)
            {
                struct data_block *addr2_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr3, addr2_blk) == -1)
                    return -1;
                short int *addr2 = addr2_blk->data;
                int pos2 = 0;
                while (flag != 3 && pos2 < addr2_blk->size)
                {
                    struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                        return -1;
                    short int *addr1 = addr1_blk->data;
                    int pos1 = 0;
                    while (flag != 3 && pos1 < addr1_blk->size)
                    {
                        flag = write_on_blk(FIRST_BLK + *addr1, flag, buf, &offset, &temp_size, size);
                        if (flag == -2)
                        {
                            printf("SFS_write：offset越界，函数结束返回\n\n");
                            return -EFBIG;
                        }
                        else if (flag == -1)
                            return -1;
                        addr1++;
                        pos1 += sizeof(short int);
                    }
                    while (flag != 3 && pos1 < 508) // 还没写完但是没有此文件的块不够了, 申请数据块
                    {
                        *addr1 = enlarge_blk();
                        if (*addr1 == -1)
                        {
                            printf("SFS_write：文件还未写完，申请空闲块失败，函数结束返回\n\n");
                            return -errno;
                        }
                        flag = write_on_blk(FIRST_BLK + *addr1, flag, buf, &offset, &temp_size, size);
                        if (flag == -2)
                        {
                            printf("SFS_write：offset越界，函数结束返回\n\n");
                            return -EFBIG;
                        }
                        else if (flag == -1)
                            return -1;
                        addr1_blk->size += sizeof(short int);
                        addr1++;
                        pos1 += sizeof(short int);
                    }
                    // 写回一级地址块
                    if (write_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                        return -1;
                    addr2++;
                    pos2 += sizeof(short int);
                }
                while (flag != 3 && pos2 < 508) // 还没写完但是没有此文件的块不够了, 申请一级地址块
                {
                    *addr2 = enlarge_blk();
                    if (*addr2 == -1)
                    {
                        printf("SFS_write：文件还未写完，申请一级地址块失败，函数结束返回\n\n");
                        return -errno;
                    }
                    struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                        return -1;
                    short int *addr1 = addr1_blk->data;
                    int pos1 = 0;
                    while (flag != 3 && pos1 < 508)
                    {
                        *addr1 = enlarge_blk();
                        if (*addr1 == -1)
                        {
                            printf("SFS_write：文件还未写完，申请空闲块失败，函数结束返回\n\n");
                            return -errno;
                        }
                        flag = write_on_blk(FIRST_BLK + *addr1, flag, buf, &offset, &temp_size, size);
                        if (flag == -2)
                        {
                            printf("SFS_write：offset越界，函数结束返回\n\n");
                            return -EFBIG;
                        }
                        else if (flag == -1)
                            return -1;
                        addr1_blk->size += sizeof(short int);
                        addr1++;
                        pos1 += sizeof(short int);
                    }
                    // 写回一级地址块
                    if (write_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                        return -1;
                    addr2_blk->size += sizeof(short int);
                    addr2++;
                    pos2 += sizeof(short int);
                }
                // 写回二级地址块
                if (write_data_block(FIRST_BLK + this_inode->addr[i], addr2_blk) == -1)
                    return -1;
                addr3++;
                pos3 += sizeof(short int);
            }
            while (flag != 3 && pos3 < 508)
            {
                *addr3 = enlarge_blk();
                if (*addr3 == -1)
                {
                    printf("SFS_write：文件还未写完，申请二级地址块失败，函数结束返回\n\n");
                    return -errno;
                }
                struct data_block *addr2_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr3, addr2_blk) == -1)
                    return -1;
                short int *addr2 = addr2_blk->data;
                int pos2 = 0;
                while (flag != 3 && pos2 < 508) // 还没写完但是没有此文件的块不够了, 申请一级地址块
                {
                    *addr2 = enlarge_blk();
                    if (*addr2 == -1)
                    {
                        printf("SFS_write：文件还未写完，申请一级地址块失败，函数结束返回\n\n");
                        return -errno;
                    }
                    struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                        return -1;
                    short int *addr1 = addr1_blk->data;
                    int pos1 = 0;
                    while (flag != 3 && pos1 < 508)
                    {
                        *addr1 = enlarge_blk();
                        if (*addr1 == -1)
                        {
                            printf("SFS_write：文件还未写完，申请空闲块失败，函数结束返回\n\n");
                            return -errno;
                        }
                        flag = write_on_blk(FIRST_BLK + *addr1, flag, buf, &offset, &temp_size, size);
                        if (flag == -2)
                        {
                            printf("SFS_write：offset越界，函数结束返回\n\n");
                            return -EFBIG;
                        }
                        else if (flag == -1)
                            return -1;
                        addr1_blk->size += sizeof(short int);
                        addr1++;
                        pos1 += sizeof(short int);
                    }
                    // 写回一级地址块
                    if (write_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                        return -1;
                    addr2_blk->size += sizeof(short int);
                    addr2++;
                    pos2 += sizeof(short int);
                }
                // 写回二级地址块
                if (write_data_block(FIRST_BLK + *addr3, addr2_blk) == -1)
                    return -1;
                addr3_blk->size += sizeof(short int);
                addr3++;
                pos3 += sizeof(short int);
            }
            // 写回三级地址块
            if (write_data_block(FIRST_BLK + this_inode->addr[i], addr3_blk) == -1)
                return -1;
        }
    }
    // 写回inode
    this_inode->st_size = offset + (size - temp_size);
    FILE *fp = NULL;
    fp = fopen(disk_path, "r+");
    if (fp == NULL)
    {
        printf("打开文件失败，文件不存在， inode写回失败\n");
        return 0;
    }
    fseek(fp, FIRST_INODE * FS_BLOCK_SIZE + this_inode->st_ino * 64, SEEK_SET);
    fwrite(this_inode, sizeof(struct inode), 1, fp);
    fclose(fp);
    free(attr);
    free(this_inode);
    printf("SFS_write：文件写入成功，函数结束返回\n\n");
    return size - temp_size;
}
// 创建目录
static int SFS_mkdir(const char *path, mode_t mode)
{
    return create_file_dir(path, 2);
}
// 删除目录
static int SFS_rmdir(const char *path)
{
    return remove_file_dir(path, 2);
}
// 进入目录
static int SFS_access(const char *path, int flag)
{
    return 0;
}
// 终端中ls -l读取目录的操作会使用到这个函数，因为fuse创建出来的文件系统是在用户空间上的
// 这个函数用来读取目录，并将目录里面的文件名加入到buf里面
static int SFS_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    printf("SFS_readdir 开始了\n\n");
    struct data_block *data_blk;
    struct file_directory *attr;
    data_blk = malloc(sizeof(struct data_block));
    attr = malloc(sizeof(struct file_directory));
    if (get_fd_to_attr(path, attr) == -1) // 打开path指定的文件，将文件属性读到attr中
    {
        free(attr);
        free(data_blk);
        return -ENOENT;
    }
    struct inode *attr_inode = malloc(sizeof(struct inode));
    get_inode(attr->st_ino, attr_inode);
    // 如果该路径所指对象为文件，则返回错误信息
    if ((attr_inode->st_mode & S_IFMT) == S_IFREG)
    {
        free(attr);
        free(data_blk);
        return -ENOENT;
    }
    // 无论是什么目录，先用filler函数添加 . 和 ..
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    struct file_directory *file_dir;
    char name[MAX_FILENAME + MAX_EXTENSION];
    for (int i = 0; i < 7; i++)
    {
        printf("当前i = %d    attr_inode->addr[i] = %d\n\n", i, attr_inode->addr[i]);
        if (i < 4)
        {
            if (attr_inode->addr[i] == -1)
                break;
            if (read_cpy_data_block(FIRST_BLK + attr_inode->addr[0], data_blk) == -1)
            {
                free(attr);
                free(data_blk);
                return -ENOENT;
            }
            file_dir = (struct file_directory *)data_blk->data;
            int pos = 0;
            while (pos < data_blk->size)
            {
                strcpy(name, file_dir->fname);
                if (strlen(file_dir->fext) != 0)
                {
                    strcat(name, ".");
                    strcat(name, file_dir->fext);
                }
                if (name[strlen(name) - 1] != '~' && filler(buf, name, NULL, 0, 0)) // 将文件名添加到buf里面
                    break;
                file_dir++;
                pos += sizeof(struct file_directory);
            }
        }
        else if (i == 4)
        {
            if (attr_inode->addr[i] == -1)
                break;
            struct data_block *addr1_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + attr_inode->addr[i], addr1_blk) == -1)
            {
                free(attr);
                free(data_blk);
                free(addr1_blk);
                return -ENOENT;
            }
            short int *addr1 = addr1_blk->data;
            int pos1 = 0;
            while (pos1 < addr1_blk->size)
            {
                if (read_cpy_data_block(FIRST_BLK + *addr1, data_blk) == -1)
                    return -1;
                file_dir = (struct file_directory *)data_blk->data;
                int pos = 0;
                while (pos < data_blk->size)
                {
                    strcpy(name, file_dir->fname);
                    if (strlen(file_dir->fext) != 0)
                    {
                        strcat(name, ".");
                        strcat(name, file_dir->fext);
                    }
                    if (name[strlen(name) - 1] != '~' && filler(buf, name, NULL, 0, 0)) // 将文件名添加到buf里面
                        break;
                    file_dir++;
                    pos += sizeof(struct file_directory);
                }
                pos1 += sizeof(short int);
                addr1++;
            }
        }
        else if (i == 5)
        {
            if (attr_inode->addr[i] == -1)
                break;
            struct data_block *addr2_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + attr_inode->addr[i], addr2_blk) == -1)
            {
                free(attr);
                free(data_blk);
                free(addr2_blk);
                return -ENOENT;
            }
            short int *addr2 = addr2_blk->data;
            int pos2 = 0;
            while (pos2 < addr2_blk->size)
            {
                struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                    return -1; // 一级地址块
                short int *addr1 = addr1_blk->data;
                int pos1 = 0;
                while (pos1 < addr1_blk->size)
                {
                    if (read_cpy_data_block(FIRST_BLK + *addr1, data_blk) == -1)
                        return -1;
                    file_dir = (struct file_directory *)data_blk->data;
                    int pos = 0;
                    while (pos < data_blk->size)
                    {
                        strcpy(name, file_dir->fname);
                        if (strlen(file_dir->fext) != 0)
                        {
                            strcat(name, ".");
                            strcat(name, file_dir->fext);
                        }
                        if (name[strlen(name) - 1] != '~' && filler(buf, name, NULL, 0, 0)) // 将文件名添加到buf里面
                            break;
                        file_dir++;
                        pos += sizeof(struct file_directory);
                    }
                    pos1 += sizeof(short int);
                    addr1++;
                }
                pos2 += sizeof(short int);
                addr2++;
            }
        }
        else
        {
            if (attr_inode->addr[i] == -1)
                break;
            struct data_block *addr3_blk = malloc(sizeof(struct data_block));
            if (read_cpy_data_block(FIRST_BLK + attr_inode->addr[i], addr3_blk) == -1)
            {
                free(attr);
                free(data_blk);
                free(addr3_blk);
                return -ENOENT;
            }
            short int *addr3 = addr3_blk->data;
            int pos3 = 0;
            while (pos3 < addr3_blk->size)
            {
                struct data_block *addr2_blk = malloc(sizeof(struct data_block));
                if (read_cpy_data_block(FIRST_BLK + *addr3, addr2_blk) == -1)
                    return -1; // 一级地址块
                short int *addr2 = addr2_blk->data;
                int pos2 = 0;
                while (pos2 < addr2_blk->size)
                {
                    struct data_block *addr1_blk = malloc(sizeof(struct data_block));
                    if (read_cpy_data_block(FIRST_BLK + *addr2, addr1_blk) == -1)
                        return -1; // 一级地址块
                    short int *addr1 = addr1_blk->data;
                    int pos1 = 0;
                    while (pos1 < addr1_blk->size)
                    {
                        if (read_cpy_data_block(FIRST_BLK + *addr1, data_blk) == -1)
                            return -1;
                        file_dir = (struct file_directory *)data_blk->data;
                        int pos = 0;
                        while (pos < data_blk->size)
                        {
                            strcpy(name, file_dir->fname);
                            if (strlen(file_dir->fext) != 0)
                            {
                                strcat(name, ".");
                                strcat(name, file_dir->fext);
                            }
                            if (name[strlen(name) - 1] != '~' && filler(buf, name, NULL, 0, 0)) // 将文件名添加到buf里面
                                break;
                            file_dir++;
                            pos += sizeof(struct file_directory);
                        }
                        pos1 += sizeof(short int);
                        addr1++;
                    }
                    pos2 += sizeof(short int);
                    addr2++;
                }
                pos3 += sizeof(short int);
                addr3++;
            }
        }
    }
    free(attr);
    free(data_blk);
    return 0;
    // filler的定义：
    //	typedef int (*fuse_fill_dir_t) (void *buf, const char *name, const struct stat *stbuf, off_t off);
    //	其作用是在readdir函数中增加一个目录项或者文件
}
// 所有文件的操作都要放到这里，fuse会帮我们在相应的linux操作中执行这些我们写好的函数
static struct fuse_operations SFS_oper = {
    .init = SFS_init,       // 初始化
    .getattr = SFS_getattr, // 获取文件属性（包括目录的）
    .mknod = SFS_mknod,     // 创建文件
    .unlink = SFS_unlink,   // 删除文件
    .open = SFS_open,       // 无论是read还是write文件，都要用到打开文件
    .read = SFS_read,       // 读取文件内容
    .write = SFS_write,     // 修改文件内容
    .mkdir = SFS_mkdir,     // 创建目录
    .rmdir = SFS_rmdir,     // 删除目录
    .access = SFS_access,   // 进入目录
    .readdir = SFS_readdir, // 读取目录
};
int main(int argc, char *argv[])
{
    umask(0);
    return fuse_main(argc, argv, &SFS_oper, NULL);
}
/*
  通过上述的分析可以知道，使用FUSE必须要自己实现对文件或目录的操作， 系统调用也会最终调用到用户自己实现的函数。
  用户实现的函数需要在结构体fuse_operations中注册。而在main()函数中，用户只需要调用fuse_main()函数就可以了，剩下的复杂工作可以交给FUSE。
*/