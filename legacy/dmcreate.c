#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#define DM_MAX_TYPE_NAME 16
struct dm_target_spec {
    uint64_t sector_start;
    uint64_t length;
    int32_t status;
    uint32_t next;
    char target_type[DM_MAX_TYPE_NAME];
};
struct dm_ioctl {
    uint32_t version[3];
    uint32_t data_size;
    uint32_t data_start;
    uint32_t target_count;
    int32_t open_count;
    uint32_t flags;
    uint32_t event_nr;
    uint32_t padding;
    uint64_t dev;
    char name[128];
    char uuid[129];
    char data[7];
};
#define DM_DEV_CREATE    _IOWR(0xfd, 3, struct dm_ioctl)
#define DM_DEV_REMOVE    _IOWR(0xfd, 4, struct dm_ioctl)
#define DM_DEV_SUSPEND   _IOWR(0xfd, 6, struct dm_ioctl)
#define DM_TABLE_LOAD    _IOWR(0xfd, 9, struct dm_ioctl)
#define DM_TABLE_STATUS  _IOWR(0xfd, 12, struct dm_ioctl)
#define DM_DEV_STATUS    _IOWR(0xfd, 7, struct dm_ioctl)

static struct dm_ioctl *mkiov(const char *name, size_t total){
    struct dm_ioctl *io = calloc(1,total);
    io->version[0]=4; io->version[1]=0; io->version[2]=0;
    io->data_size=(uint32_t)total;
    io->data_start=(uint32_t)sizeof(struct dm_ioctl);
    snprintf(io->name,sizeof io->name,"%s",name);
    return io;
}
int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    mkdir("/dev/mapper",0755);
    if(argc>=3 && !strcmp(argv[1],"-t")){
        int f=open("/dev/mapper/control",O_RDWR);
        if(f<0){ printf("open control: %s\n",strerror(errno)); return 1; }
        size_t tot=sizeof(struct dm_ioctl)+8192;
        struct dm_ioctl *io=mkiov(argv[2],tot);
        if(ioctl(f,DM_TABLE_STATUS,io)<0){ printf("TABLE_STATUS: %s\n",strerror(errno)); return 1; }
        printf("status dev=0x%llx targets=%u data_start=%u size=%u\n",(unsigned long long)io->dev,io->target_count,io->data_start,io->data_size);
        { unsigned char *d=(unsigned char*)io+io->data_start; printf("raw:"); for(int i=0;i<200;i++) printf(" %02x",d[i]); printf("\n");
          printf("ascii:"); for(int i=0;i<200;i++){ unsigned char ch=d[i]; printf("%c",(ch>=32&&ch<127)?ch:'.'); } printf("\n"); }
        struct dm_target_spec *sp=(struct dm_target_spec*)((char*)io+io->data_start);
        for(unsigned i=0;i<io->target_count;i++){
            printf("target[%u] start=%llu len=%llu type=%s params=%s\n",i,
                (unsigned long long)sp->sector_start,(unsigned long long)sp->length,
                sp->target_type,(char*)(sp+1));
            if(!sp->next) break;
            sp=(struct dm_target_spec*)((char*)sp+sp->next);
        }
        return 0;
    }
    if(argc>=3 && !strcmp(argv[1],"-s")){
        int f=open("/dev/mapper/control",O_RDWR);
        if(f<0){ printf("open control: %s\n",strerror(errno)); return 1; }
        size_t tot=sizeof(struct dm_ioctl)+8;
        struct dm_ioctl *io=mkiov(argv[2],tot);
        if(ioctl(f,DM_DEV_STATUS,io)<0){ printf("DEV_STATUS: %s\n",strerror(errno)); return 1; }
        printf("dev=0x%llx open=%d targets=%u flags=0x%x\n",(unsigned long long)io->dev,io->open_count,io->target_count,io->flags);
        return 0;
    }
    if(argc>=3 && !strcmp(argv[1],"-r")){
        int f=open("/dev/mapper/control",O_RDWR);
        if(f<0){ printf("open control: %s\n",strerror(errno)); return 1; }
        struct dm_ioctl *io=mkiov(argv[2],sizeof(struct dm_ioctl)+8);
        if(ioctl(f,DM_DEV_REMOVE,io)<0){ printf("DEV_REMOVE: %s\n",strerror(errno)); return 1; }
        printf("removed %s\n",argv[2]); return 0;
    }
    if(argc<6){ printf("usage: dmcreate <name> <start> <length> <type> <params...> | -r <name>\n"); return 64; }
    const char *name=argv[1];
    unsigned long long start=strtoull(argv[2],0,10), len=strtoull(argv[3],0,10);
    const char *type=argv[4];
    char params[4096]; params[0]=0;
    for(int i=5;i<argc;i++){ if(i>5) strncat(params," ",sizeof params-strlen(params)-1);
        strncat(params,argv[i],sizeof params-strlen(params)-1); }
    size_t total=sizeof(struct dm_ioctl)+sizeof(struct dm_target_spec)+strlen(params)+1;
    struct dm_ioctl *io=mkiov(name,total);
    io->target_count=1;
    struct dm_target_spec *spec=(struct dm_target_spec*)((char*)io+sizeof(struct dm_ioctl));
    spec->sector_start=start; spec->length=len; spec->status=0; spec->next=0;
    snprintf(spec->target_type,sizeof spec->target_type,"%s",type);
    memcpy((char*)(spec+1),params,strlen(params)+1);
    printf("table %llu..%llu type=%s params=%s\n",start,len,type,params);
    int fd=open("/dev/mapper/control",O_RDWR);
    if(fd<0){ printf("open control: %s\n",strerror(errno)); return 1; }
    if(ioctl(fd,DM_DEV_CREATE,io)<0){ printf("DEV_CREATE: %s\n",strerror(errno)); return 1; }
    printf("created %s dev=0x%llx\n",name,(unsigned long long)io->dev);
    { char node[256]; snprintf(node,sizeof node,"/dev/mapper/%s",name); unlink(node);
      unsigned maj=(unsigned)(io->dev>>8)&0xfff, min=(unsigned)(io->dev&0xff)|((unsigned)(io->dev>>12)&0xfff00);
      if(mknod(node,S_IFBLK|0600,makedev(maj,min))==0) printf("node %s = %u:%u\n",node,maj,min);
      else printf("mknod: %s\n",strerror(errno)); }
    struct dm_ioctl *io2=mkiov(name,total);
    io2->target_count=1;
    memcpy((char*)io2+sizeof(struct dm_ioctl),spec,sizeof(struct dm_target_spec)+strlen(params)+1);
    if(ioctl(fd,DM_TABLE_LOAD,io2)<0){ printf("TABLE_LOAD: %s\n",strerror(errno)); return 1; }
    printf("table loaded\n");
    struct dm_ioctl *io3=mkiov(name,sizeof(struct dm_ioctl)+8);
    if(ioctl(fd,DM_DEV_SUSPEND,io3)<0){ printf("SUSPEND(resume): %s\n",strerror(errno)); return 1; }
    printf("resumed (device active)\n");
    return 0;
}