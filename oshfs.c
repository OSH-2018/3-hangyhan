//算法思想
//文件在mem[1] ~ mem[n]上分块存储，mem[i]由mmap映射到进程地址空间中
//mem[0]存储文件的分块信息表，以[file_name][blocknum[]]为一行
//文件信息（文件名）

#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <fuse.h>
#include <sys/mman.h>
#define SIZE 4*1024*1024*1024 //文件系统大小
#define BLOCKSIZE 64*1024 //文件分块大小
#define INDEXSIZE 1024*1024 //索引表大小
#define BLOCKNUM 64*1024 //block总数 

int filenum = 0; //文件个数

typedef struct indexline{//索引项
    char file_name[ 20 ] ;
    struct stat st ; //文件属性
    int blocknum ; //文件分块数
    int blockno[ BLOCKNUM ]; //记录文件逻辑地址

}indexline, *indextable, *indexpointer;

/*struct filenode {//文件节点
    char *filename;
    struct stat *st;
    //struct filenode *next;
    void *content;
};*/

//static const size_t size = 4 * 1024 * 1024 * (size_t)1024;

static void *mem[ BLOCKNUM ]; //逻辑地址空间
//static struct filenode *root = NULL;  //文件系统的根节点


static void *oshfs_init(struct fuse_conn_info *conn) //初始化文件系统 FINISHED

{
    mem[0] = ( indextable )mmap(NULL, INDEXSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); //分配索引表地址空间
    memset( mem + 1, 0, 4*(BLOCKNUM-1) );
    filenum = 0;
    return NULL;

}


int get_filenode(const char *name)//查找文件并返回文件在索引表中的序号 FINISHED

{

    int i = 0;

    while( i < filenum ) {

        if(strcmp( (( indexpointer )(mem[ 0 ] + i)) -> file_name, name + 1) != 0)
            i ++;
        else
            return i;
    }
    return -1;

}



static void create_filenode(const char *filename, const struct stat *st)//创建一个名为filename的文件并保存它的属性  FINISHED

{

    /*查找第一个没有分配block的逻辑地址
    int i = 1;
    while( mem[ i ] && i < SIZE / BLOCKSIZE ) i++ ;
    if( i == SIZE / BLOCKSIZE )exit();

    //为文件分配存储空间
    mem[ i ] = mmap(NULL, BLOCKSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memcpy( mem[ i ], st, sizeof( struct stat ) );*/

    //为文件添加索引和相关信息
    strcpy( ( ( indexpointer )( mem[ 0 ] + filenum ) ) -> file_name,  filename );
    (( ( indexpointer )( mem[ 0 ] + filenum ) ) -> st).st_mode = st -> st_mode;
    (( ( indexpointer )( mem[ 0 ] + filenum ) ) -> st).st_uid = st -> st_uid;
    (( ( indexpointer )( mem[ 0 ] + filenum ) ) -> st).st_gid = st -> st_gid;
    (( ( indexpointer )( mem[ 0 ] + filenum ) ) -> st).st_nlink = st -> st_nlink;
    (( ( indexpointer )( mem[ 0 ] + filenum ) ) -> st).st_size = st -> st_size;
    ( ( indexpointer )( mem[ 0 ] + filenum ) ) -> blocknum = 0;

    //修改文件系统信息
    filenum ++ ;

    return  ;

}






static int oshfs_getattr(const char *path, struct stat *stbuf)//把path下的文件的属性拷贝到stbuf中 FINISHED

{

    int ret = 0;

    int filepos = get_filenode(path);

    if(strcmp(path, "/") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
    } else if( filepos >= 0) {
        memcpy(stbuf,  &( ( (indexpointer)( mem[ 0 ] + filepos ) ) -> st ), sizeof(struct stat));
    } else {
        ret = -ENOENT;
    }
    return ret;
}



static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) //返回path路径下的文件列表

{

    int i = 0;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    while( i < filenum ) {
        filler(buf, (( indexpointer )( mem[ 0 ] + i )) -> file_name, &( ((indexpointer)( mem[ 0 ] + i)) -> st ), 0);
        i ++;
    }
    return 0;
}



static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)  //向文件系统中添加一个文件 FINISHED

{

    struct stat st;
    st.st_mode = S_IFREG | 0644;
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;
    st.st_size = 0;
    create_filenode(path + 1, &st);
    return 0;

}



static int oshfs_open(const char *path, struct fuse_file_info *fi)// FINISHED

{
    return 0;
}



static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)//向已经存在的文件中追加内容 FINISHED

{
    int filepos = get_filenode(path);
    indexpointer fileindex = ((indexpointer)mem[ 0 ]) + filepos ;
    int start[ 2 ];
    int i; //循环变量


    //如果空间不够则要添加存储块并且修改文件大小
    if( offset + size > ( fileindex -> blocknum ) * BLOCKSIZE )( fileindex -> st ).st_size = offset + size;
    while( offset + size > ( fileindex -> blocknum ) * BLOCKSIZE ){
        for( i = 1; i < BLOCKNUM ; i++ )
            if( !mem[ i ] )break;
        if( i == BLOCKNUM )exit(0);
        mem[ i ] = mmap(NULL, BLOCKSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ( fileindex -> blockno )[ fileindex -> blocknum ] = i;
        ( fileindex -> blocknum )++;

    }
    

    //写文件
    char *write_pointer = (char *)buf;
    int k = offset / BLOCKSIZE ; //从第k块的第j个字节开始写入数据
    int j = offset % BLOCKSIZE ;
    int if_firstblock = 1;
    int remain = size;

    if( ( size + j ) <= BLOCKSIZE ){ //对于不需要跨块写入的情况单独处理
        memcpy( mem[ ( fileindex -> blockno )[ k ] ] + j, write_pointer, size );
        return size;
    }

    while( remain > BLOCKSIZE ){
        if( if_firstblock ){//对起始块的写入需要进行特殊处理
            if_firstblock = 0;
            memcpy( mem[ ( fileindex -> blockno )[ k ] ] + j, write_pointer, BLOCKSIZE - j);
            write_pointer += ( BLOCKSIZE - j );
            remain -= ( BLOCKSIZE - j );
        }
        else{//对一般的情况就直接将这块填满
            memcpy( mem[ ( fileindex -> blockno )[ k ] ], write_pointer, BLOCKSIZE);
            write_pointer +=  BLOCKSIZE ;
            remain -= BLOCKSIZE;
        }
        k++;
    }

    memcpy( mem[ ( fileindex -> blockno )[ k ] ], write_pointer, remain ); //对最后一块单独处理


    return size;
}



static int oshfs_truncate(const char *path, off_t size)//调整文件大小 FINISHED

{
    int fileops = get_filenode(path);
    int new_blocknum = ( size - 1 ) /  BLOCKSIZE + 1;
    int i, j;
    indexpointer fileindex = mem[ 0 ] + fileops;

    //修改文件大小
    ( fileindex -> st ).st_size = size;

    //根据new_blocknum分配或者释放块
    if( new_blocknum > ( fileindex -> blocknum ) ){//如果new_blocknum > blocknum就要分配新的块
        for( i = fileindex -> blocknum; i < new_blocknum; i++ ){

            for( j = 1; j < BLOCKNUM ; j++ )
                if( !mem[ j ] )break;
            if( j == BLOCKNUM )exit(0);

            mem[ j ] = mmap(NULL, BLOCKSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            ( fileindex -> blockno )[ i ] = j;

        }
    }
    else{
        if( new_blocknum < fileindex -> blocknum){//如果new_blocknum < blocknum就要释放多余的块

            for( i = new_blocknum; i < fileindex -> blocknum; i++ )munmap( mem[ ( fileindex -> blockno )[ i ] ], BLOCKSIZE);

        }
    }

    //修改文件块数的信息
    fileindex -> blocknum = new_blocknum;

    return 0;
}



static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)//读取文件 FINISHED

{
   
    int filepos = get_filenode(path);
    int ret = size;
    int filesize = ( ( ( indexpointer )( mem[ 0 ] + filepos ) ) -> st ).st_size;
    int tail ;//读到第几位停止
    int start[ 2 ], end[ 2 ];


    //计算文件读到多少位停止
    if( offset + size > filesize ){
        ret = filesize - offset;
        tail = filesize - 1;
    }
    else tail = offset + size - 1 ;

    //计算读的起点位置
    start[ 0 ] = offset / BLOCKSIZE ;
    start[ 1 ] = offset % BLOCKSIZE ;

    //读取文件
    int i;
    int remain = ret;
    char *pointer = buf;
    int *blockno =( ( indexpointer )( mem[ 0 ] + filepos ) ) -> blockno;

    if( ( ret + start[ 1 ] ) <= BLOCKSIZE ){ //对于不需要跨块read的情况单独处理
        memcpy( pointer, mem[ blockno[ start[ 0 ] ] ] + start[ 1 ], ret );
        *( pointer + 1 ) = '\0';
        return ret;
    }
    
    //对于第一块单独处理
    memcpy( pointer, mem[ blockno[ start[ 0 ] ] ] + start[ 1 ], BLOCKSIZE - start[ 1 ]);
    pointer += BLOCKSIZE - start[ 1 ];
    remain -= ( BLOCKSIZE - start[ 1 ]);
    i = start[ 0 ] + 1;

    while( remain > BLOCKSIZE ){
        memcpy( pointer, mem[ blockno[ i ] ], BLOCKSIZE);
        pointer += BLOCKSIZE;
        i++;
    }

    memcpy( pointer, mem[ blockno[ i ] ] , remain);//对最后一块单独处理
    *( pointer + 1 ) = '\0';
    

    return ret;
}



static int oshfs_unlink(const char *path)//删除文件 FINISHED

{

    int filepos = get_filenode(path);
    indexpointer fileindex = mem[ 0 ] + filepos;
    int i;

    //munmap所有已经分配的块
    for( i = 0; i < ( fileindex -> blocknum ); i++ ){
        munmap( mem[ (fileindex -> blockno)[ i ] ], BLOCKSIZE );
    }

    //整理文件索引表
    indexpointer fileindex1 ;
    indexpointer fileindex2 ;
    for( i = filepos; i < ( filenum - 1 ); i++ ){
        memcpy( fileindex1, fileindex2, sizeof( indexline ) );
    }

    //修改文件系统大小
    filenum --;

    return 0;

}



static const struct fuse_operations op = {

    .init = oshfs_init,
    .getattr = oshfs_getattr,
    .readdir = oshfs_readdir,
    .mknod = oshfs_mknod,
    .open = oshfs_open,
    .write = oshfs_write,
    .truncate = oshfs_truncate,
    .read = oshfs_read,
    .unlink = oshfs_unlink,

};



int main(int argc, char *argv[])

{

    return fuse_main(argc, argv, &op, NULL);

}
