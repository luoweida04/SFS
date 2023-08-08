#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
// 注：块号从0开始
#define FS_BLOCK_SIZE 512 // 块大小512字节
#define SUPER_BLOCK 1 // 一个超级块
#define FIRST_BLK 518 // 数据区的第一块块号
#define DATASIZE 15866 //数据区大小，以块为单位 
#define FIRST_INODE 6 //inode区起始块号
#define INODE_AREA_SIZE 512 //inode区大小，以块为单位
#define FIRST_BLK_OF_INODEBITMAP 1 //inode位图区起始块号
#define INODEBITMAP_SIZE 1 // inode位图区大小，以块为单位
#define FIRST_BLK_OF_DATABITMAP 2 //数据块位图起始块号
#define DATABITMAP_SIZE 4 //数据块位图大小，以块为单位
//超级块中记录的，大小为 72 bytes（9个long），占用1块磁盘块
struct sb {
    long fs_size;  //文件系统的大小，以块为单位
    long first_blk;  //数据区的第一块块号，根目录也放在此
    long datasize;  //数据区大小，以块为单位 
    long first_inode;    //inode区起始块号
    long inode_area_size;   //inode区大小，以块为单位
    long first_blk_of_inodebitmap;   //inode位图区起始块号
    long inodebitmap_size;  // inode位图区大小，以块为单位
    long first_blk_of_databitmap;   //数据块位图起始块号
    long databitmap_size;      //数据块位图大小，以块为单位
};
struct inode { // 每个inode64字节
    int st_mode; /* 权限，4字节, 因为64字节有剩余，我把权限拓展到32bit */ 
    short int st_ino; /* i-node号，2字节,从0开始*/ 
    char st_nlink; /* 连接数，1字节 */ 
    uid_t st_uid; /* 拥有者的用户 ID ，4字节 */ 
    gid_t st_gid; /* 拥有者的组 ID，4字节  */ 
    off_t st_size; /*文件大小，4字节 */ 
    struct timespec st_atim; /* 16个字节time of last access */ 
    /*磁盘地址，14字节, addr[0]-addr[3]是直接地址，addr[4]是一次间接，addr[5]是二次间接，addr[6]是三次间接。*/
    short int addr [7]; // 从0开始，用的是偏移量
};
//记录文件信息的数据结构，每个目录项为16B
struct file_directory {
    char fname[8]; // 文件名 ，8字节
    char fext[3]; // 扩展名 ，3字节
    short int st_ino; // i-node号，2字节，实际使用12位
    char reserve[3]; // 备用3字节
};
//文件内容存放用到的数据结构，大小为 512 bytes，占用1块磁盘块
struct data_block {
    int size; // 多少字节
    char data[508];
};
/*磁盘（8M文件）的初始化步骤：
    首先文件系统有1个超级块，1个inode位图块，4个数据块位图块，512个inode区，15866个数据区，
    1个根目录信息块，diskimg应该有1+1+4+1(根目录)=7块是被分配了的。
    1. 超级块初始化：
        ① 通过计算可以得到fs_size = 8MB/512Bytes = 16384 块 
        ② 然后 根目录信息块位置 应该是1281(前面 0~1280 共 1281 块）的块空间都被超级块和bitmap占用了)
                但是我们设置的时候first_blk设置为偏移量：1281，因为从0移动 1281 bit
                后的位置是1281，之后所有文件的块位置都用偏移量来表示而不是直接给出块的位置
        ③ 最后bitmap的大小是1280块
    2. 1280块bitmap的初始化：
        首先要清楚，前面1282bit(0~1281)为1，后面的为0，又因为1282/32=40余2，一个整数32位，所以只需要将
        一个大小为40的整形数组赋值为-1，（-1时整数的32bit全1，补码表示），然后再专门用一个整数移位30bit和31bit
        1280、1281这bit也为1，就完成了'1’的初始化。剩余的就全部都是初始化为0了，又因为10240bit只需要3个block
        3*512*8=12288>10240 就可以表示了，每个block 4096bit， 而我们刚才只用了1280+32bit，第一个block都没有
        完全初始化，所以这个block剩余的 4096-1312 = 2784bit 要完成初始化，2784bit刚好等于87个整数，所以直接将
        一个大小为87的整形数组继续写到文件中就可以了
        然后剩余2个block共8192bit，可以用大小为256的 全0 整形数组 来初始化，这样bitmap的初始化就大功告成了
    3. 然后我们要对第1282块（即编号为1281块位置的根目录块）进行初始化，因为一开始所有块都是空的，所以我们习惯性地将
        根目录块放在1281位置的块中，并且这个块我们默认是知道这就是根目录的 数据块 其他文件要使用块时不能使用它
        在后面文件安排中，我们可能会用 首次适应或者最优适应 这种方法来存放文件
        那么初始化第1282块，直接写入1个data_block（512byte）就可以了
*/
int main(){
    FILE *fp=NULL;
    fp = fopen("/home/luoweida/桌面/SFS/diskimg", "r+");//打开文件
	if (fp == NULL) {
		printf("打开文件失败，文件不存在\n");return 0;
    }
    //1. 初始化super_block     大小：1块
    struct sb *super_blk = malloc(sizeof(struct sb));//动态内存分配，申请super_blk
    super_blk->fs_size=16384;
    super_blk->first_blk=FIRST_BLK;//根目录的data_block在518编号的块中（从0开始编号）
    super_blk->datasize=DATASIZE;  //数据区大小，以块为单位 
    super_blk->first_inode=FIRST_INODE;    //inode区起始块号
    super_blk->inode_area_size=INODE_AREA_SIZE;   //inode区大小，以块为单位
    super_blk->first_blk_of_inodebitmap=FIRST_BLK_OF_INODEBITMAP;   //inode位图区起始块号
    super_blk->inodebitmap_size=INODEBITMAP_SIZE;  // inode位图区大小，以块为单位
    super_blk->first_blk_of_databitmap=FIRST_BLK_OF_DATABITMAP;   //数据块位图起始块号
    super_blk->databitmap_size=DATABITMAP_SIZE;      //数据块位图大小，以块为单位
    fwrite(super_blk, sizeof(struct sb), 1, fp);
    printf("initial super_block success!\n");

    //2. 初始化位图
    if (fseek(fp, FS_BLOCK_SIZE * 1, SEEK_SET) != 0) // 首先要将指针移动到文件的块号1的起始位置512
        fprintf(stderr, "inode_bitmap fseek failed!\n");
        // inode位图
    int mask = 1;
	mask <<= 31; // 第一个bit是根目录
    printf("mask: %d\n\n", mask);
    fwrite(&mask, sizeof(int), 1, fp);
    int a[127];
    memset(a,0,sizeof(a));
    fwrite(a,sizeof(a),1,fp);
    printf("initial inode_bitmap success!\n");
        // 数据块位图
    int mask1 = 1;
    mask1 <<= 31; // 第一个bit作为根目录文件
    fwrite(&mask1, sizeof(int), 1, fp);
    int d[511];
    memset(d,0,sizeof(d));
    fwrite(d,sizeof(d),1,fp);
        // inode区
    struct inode *root_inode = malloc(sizeof(struct inode));//动态内存分配，申请root_inode
    root_inode->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR;// 目录文件，可读/写/执行
    root_inode->st_ino = 0; /* i-node号，2字节 */ 
    root_inode->st_nlink = 0; /* 连接数，1字节 */ 
    root_inode-> st_uid = getuid(); /* 拥有者的用户 ID ，4字节 */ 
    root_inode->st_gid = getgid(); /* 拥有者的组 ID，4字节  */ 
    root_inode->st_size = 0; /*文件大小，0字节 */ 
    timespec_get(&root_inode->st_atim, TIME_UTC);
    // memset(root_inode->addr,-1,sizeof(root_inode->addr));
    for(int i=0;i<7;i++) root_inode->addr[i] = -1;
    root_inode->addr[0]=0; // 0是偏移量，指的是0号数据块
    fwrite(root_inode, sizeof(struct inode), 1, fp);
    printf("initial root_inode success!\n");

        // 数据块, 所有数据块size=0
    int pos = 0;
    fseek(fp, FIRST_BLK * FS_BLOCK_SIZE, SEEK_SET);
    struct data_block *root = malloc(sizeof(struct data_block));
	root->size = 0;
    root->data[0]='\0';
    while(pos < DATASIZE) {
        fwrite(root, sizeof(struct data_block), 1, fp); //写入磁盘，初始化完成
        pos++;
    }
    printf("initial data_block success!\n");
    fclose(fp);
    printf("super_inode_data_blocks init success!\n");
}