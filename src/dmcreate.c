/* dmcreate.c — device-mapper 建表工具（可移植版）
 *
 * 低层用法（和以前一样）：
 *   dmcreate <name> <start> <length> <type> "<params>"
 *   dmcreate -r <name>            删除
 *   dmcreate -s <name>            状态
 *   dmcreate -t <name>            打印表（含原始字节，便于排错）
 *
 * 高层用法（FBE 解锁常用）：
 *   dmcreate -k <name> <blockdev> <keyfile> [--target dm-default-key]
 *            [--sector-size 4096] [--sectors N] [--start 0]
 *            [--params "sector_size:4096 iv_large_sectors wrappedkey_v0"]
 *            [--plain]        # 不追加 wrappedkey 相关参数（普通 dm-crypt 场景）
 *
 * keyfile：原始密钥字节。Android FBE 的 dm-default-key 是 100 字节（200 hex）；
 *          普通 dm-crypt aes-xts-plain64 是 64 字节（128 hex）。按文件尺寸自动转换。
 * length：不传 --sectors 时用 BLKGETSIZE64/sector-size 自动算。
 *
 * 所有参数都有环境变量等价物（FBE_DM_*），便于别的设备写 profile。
 */
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
#include <linux/fs.h>

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

static struct dm_ioctl *mkiov(const char *name, size_t total) {
    struct dm_ioctl *io = calloc(1, total);
    io->version[0] = 4; io->version[1] = 0; io->version[2] = 0;
    io->data_size = (uint32_t)total;
    io->data_start = (uint32_t)sizeof(struct dm_ioctl);
    snprintf(io->name, sizeof io->name, "%s", name);
    return io;
}

static int dm_open(void) {
    mkdir("/dev/mapper", 0755);
    int f = open("/dev/mapper/control", O_RDWR);
    if (f < 0) printf("open /dev/mapper/control: %s\n", strerror(errno));
    return f;
}

static int mknod_for(struct dm_ioctl *io, const char *name) {
    char node[256]; snprintf(node, sizeof node, "/dev/mapper/%s", name);
    unlink(node);
    unsigned maj = (unsigned)(io->dev >> 8) & 0xfff;
    unsigned min = (unsigned)(io->dev & 0xff) | ((unsigned)(io->dev >> 12) & 0xfff00);
    if (mknod(node, S_IFBLK | 0600, makedev(maj, min)) == 0) {
        printf("node %s = %u:%u\n", node, maj, min);
        return 0;
    }
    printf("mknod %s: %s\n", node, strerror(errno));
    return -1;
}

static int create_table(const char *name, unsigned long long start, unsigned long long len,
                        const char *type, const char *params) {
    size_t total = sizeof(struct dm_ioctl) + sizeof(struct dm_target_spec) + strlen(params) + 1;
    struct dm_ioctl *io = mkiov(name, total);
    io->target_count = 1;
    struct dm_target_spec *spec = (struct dm_target_spec *)((char *)io + sizeof(struct dm_ioctl));
    spec->sector_start = start; spec->length = len; spec->status = 0; spec->next = 0;
    snprintf(spec->target_type, sizeof spec->target_type, "%s", type);
    memcpy((char *)(spec + 1), params, strlen(params) + 1);

    printf("table %llu..%llu type=%s params=%s\n", start, len, type, params);
    int fd = dm_open();
    if (fd < 0) return 1;
    if (ioctl(fd, DM_DEV_CREATE, io) < 0) { printf("DEV_CREATE: %s\n", strerror(errno)); return 1; }
    printf("created %s dev=0x%llx\n", name, (unsigned long long)io->dev);
    mknod_for(io, name);

    struct dm_ioctl *io2 = mkiov(name, total);
    io2->target_count = 1;
    memcpy((char *)io2 + sizeof(struct dm_ioctl), spec, sizeof(struct dm_target_spec) + strlen(params) + 1);
    if (ioctl(fd, DM_TABLE_LOAD, io2) < 0) { printf("TABLE_LOAD: %s\n", strerror(errno)); return 1; }
    printf("table loaded\n");

    struct dm_ioctl *io3 = mkiov(name, sizeof(struct dm_ioctl) + 8);
    if (ioctl(fd, DM_DEV_SUSPEND, io3) < 0) { printf("SUSPEND(resume): %s\n", strerror(errno)); return 1; }
    printf("resumed (device active)\n");
    return 0;
}

static int read_key_hex(const char *path, char *out, size_t cap) {
    unsigned char k[512]; size_t n = 0;
    FILE *f = fopen(path, "rb");
    if (!f) { printf("open key %s: %s\n", path, strerror(errno)); return -1; }
    n = fread(k, 1, sizeof k, f); fclose(f);
    if (n != 100 && n != 64 && n != 32) {
        printf("key 尺寸 %zu 不常见（期望 100=dm-default-key / 64=dm-crypt xts / 32=aes）\n", n);
    }
    if (n * 2 + 1 > cap) { printf("hex buffer too small\n"); return -1; }
    for (size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", k[i]);
    out[n * 2] = 0;
    return (int)n;
}

static unsigned long long dev_sectors(const char *dev, unsigned sector_size) {
    int f = open(dev, O_RDONLY);
    if (f < 0) { printf("open %s: %s\n", dev, strerror(errno)); return 0; }
    unsigned long long bytes = 0;
    if (ioctl(f, BLKGETSIZE64, &bytes) < 0) { printf("BLKGETSIZE64: %s\n", strerror(errno)); close(f); return 0; }
    close(f);
    return bytes / sector_size;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) {
        printf("usage:\n");
        printf("  dmcreate <name> <start> <length> <type> \"<params>\"\n");
        printf("  dmcreate -r <name> | -s <name> | -t <name>\n");
        printf("  dmcreate -k <name> <blockdev> <keyfile> [--target T] [--sector-size N]\n");
        printf("           [--sectors N] [--start S] [--params ...] [--plain]\n");
        return 64;
    }
    if (!strcmp(argv[1], "-r") || !strcmp(argv[1], "-s") || !strcmp(argv[1], "-t")) {
        char mode = argv[1][1];
        if (argc < 3) { printf("need name\n"); return 64; }
        int fd = dm_open(); if (fd < 0) return 1;
        const char *name = argv[2];
        if (mode == 'r') {
            struct dm_ioctl *io = mkiov(name, sizeof(struct dm_ioctl) + 8);
            if (ioctl(fd, DM_DEV_REMOVE, io) < 0) { printf("DEV_REMOVE: %s\n", strerror(errno)); return 1; }
            printf("removed %s\n", name); return 0;
        }
        if (mode == 's') {
            struct dm_ioctl *io = mkiov(name, sizeof(struct dm_ioctl) + 8);
            if (ioctl(fd, DM_DEV_STATUS, io) < 0) { printf("DEV_STATUS: %s\n", strerror(errno)); return 1; }
            printf("dev=0x%llx open=%d targets=%u flags=0x%x\n",
                   (unsigned long long)io->dev, io->open_count, io->target_count, io->flags);
            return 0;
        }
        size_t tot = sizeof(struct dm_ioctl) + 8192;
        struct dm_ioctl *io = mkiov(name, tot);
        if (ioctl(fd, DM_TABLE_STATUS, io) < 0) { printf("TABLE_STATUS: %s\n", strerror(errno)); return 1; }
        printf("status dev=0x%llx targets=%u data_start=%u size=%u\n",
               (unsigned long long)io->dev, io->target_count, io->data_start, io->data_size);
        struct dm_target_spec *sp = (struct dm_target_spec *)((char *)io + io->data_start);
        for (unsigned i = 0; i < io->target_count; i++) {
            printf("target[%u] start=%llu len=%llu type=%s params=%s\n", i,
                   (unsigned long long)sp->sector_start, (unsigned long long)sp->length,
                   sp->target_type, (char *)(sp + 1));
            if (!sp->next) break;
            sp = (struct dm_target_spec *)((char *)sp + sp->next);
        }
        return 0;
    }

    if (!strcmp(argv[1], "-k")) {
        if (argc < 5) { printf("-k 需要 <name> <blockdev> <keyfile>\n"); return 64; }
        const char *name = argv[2], *dev = argv[3], *keyf = argv[4];
        const char *target = getenv("FBE_DM_TARGET") ? getenv("FBE_DM_TARGET") : "dm-default-key";
        const char *extra  = getenv("FBE_DM_PARAMS") ? getenv("FBE_DM_PARAMS") : NULL;
        unsigned sector_size = 4096;
        unsigned long long sectors = 0, start = 0;
        int plain = 0;
        for (int i = 5; i < argc; i++) {
            if      (!strcmp(argv[i], "--target") && i+1 < argc) target = argv[++i];
            else if (!strcmp(argv[i], "--sector-size") && i+1 < argc) sector_size = (unsigned)strtoul(argv[++i], NULL, 0);
            else if (!strcmp(argv[i], "--sectors") && i+1 < argc) sectors = strtoull(argv[++i], NULL, 0);
            else if (!strcmp(argv[i], "--start") && i+1 < argc) start = strtoull(argv[++i], NULL, 0);
            else if (!strcmp(argv[i], "--params") && i+1 < argc) extra = argv[++i];
            else if (!strcmp(argv[i], "--plain")) plain = 1;
            else { printf("unknown option %s\n", argv[i]); return 64; }
        }
        char keyhex[1200];
        if (read_key_hex(keyf, keyhex, sizeof keyhex) < 0) return 1;
        if (!sectors) { sectors = dev_sectors(dev, sector_size); if (!sectors) return 1; }

        char params[2048];
        if (extra) snprintf(params, sizeof params, "%s", extra);
        else if (plain) snprintf(params, sizeof params, "aes-xts-plain64 %s 0 %s 0", keyhex, dev);
        else snprintf(params, sizeof params,
                      "aes-xts-plain64 %s 0 %s 0 sector_size:%u iv_large_sectors wrappedkey_v0",
                      keyhex, dev, sector_size);
        printf("target=%s sectors=%llu sector_size=%u\n", target, sectors, sector_size);
        return create_table(name, start, sectors, target, params);
    }

    if (argc < 6) { printf("低层用法: dmcreate <name> <start> <length> <type> \"<params>\"\n"); return 64; }
    const char *name = argv[1];
    unsigned long long start = strtoull(argv[2], NULL, 10), len = strtoull(argv[3], NULL, 10);
    const char *type = argv[4];
    char params[4096]; params[0] = 0;
    for (int i = 5; i < argc; i++) {
        if (i > 5) strncat(params, " ", sizeof params - strlen(params) - 1);
        strncat(params, argv[i], sizeof params - strlen(params) - 1);
    }
    return create_table(name, start, len, type, params);
}
