/* kmkey.c — 取 userdata 的存储密钥（可移植版）
 *
 * 做什么：把 254 字节的内层 KMKD（encrypted_key.bin 去掉 28 字节头）交给 TEE 里的
 * keymaster TA，执行 convertStorageKeyToEphemeral，拿回 100 字节的存储密钥。
 *
 * 依赖（Qualcomm smcinvoke 一代）：
 *   - /dev/smcinvoke（老设备是 /dev/qseecom，本程序只实现 smcinvoke 通道）
 *   - TA 名默认 keymaster64，可用 -T 改
 *   - 服务号默认 122，可用 -S 改
 *
 * 用法：
 *   kmkey [-D 设备] [-T TA名] [-S 服务号] [-i 内层KMKD] [-b 完整blob] [-o 原始key] [-x hex key]
 * 环境变量：FBE_SMCI_DEV / FBE_TA_NAME / FBE_TA_SVC
 *
 * 输入尺寸约定（会按尺寸自动识别）：
 *   254 字节  -> 内层 KMKD，直接用
 *   282 字节  -> encrypted_key.bin，自动跳过前 28 字节
 *   214 字节  -> keymaster 输出的 wrapped blob，需要先经 TA unwrap（本工具不含这一步）
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
#include <sys/types.h>

typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64; typedef int32_t s32; typedef int64_t s64;
struct smci_buf { u64 addr; u64 size; };
struct smci_obj { s64 fd; s64 cb_server_fd; s64 reserved; };
union smci_arg { struct smci_buf b; struct smci_obj o; };
struct smci_req { u32 op; u32 counts; s32 result; u32 argsize; u64 args; s64 reserved; };
#define INVOKE _IOWR(0x98, 1, struct smci_req)
#define PACK(bi,bo,oi,oo) ((u32)((bi) | ((bo)<<4) | ((oi)<<8) | ((oo)<<12)))

static int g_ta = -1;
static u8 g_req[16384], g_rsp[16384];

static int smci(int fd, u32 op, u32 counts, union smci_arg *args, s32 *res) {
    struct smci_req rq; memset(&rq, 0, sizeof rq);
    rq.op = op; rq.counts = counts; rq.argsize = sizeof(union smci_arg);
    rq.args = (u64)(uintptr_t)args; errno = 0;
    int r = ioctl(fd, INVOKE, &rq);
    if (res) *res = rq.result;
    return r < 0 ? -errno : 0;
}

static int ta_send(u32 cmd, const u8 *pl, u32 plen) {
    memset(g_req, 0, sizeof g_req);
    memcpy(g_req, &cmd, 4);
    if (plen) memcpy(g_req + 4, pl, plen);
    memset(g_rsp, 0, sizeof g_rsp);
    u32 is64 = 2, reqlen = plen + 4, rsplen = sizeof g_rsp;
    union smci_arg a[10]; memset(a, 0, sizeof a);
    a[0].b.addr = (u64)(uintptr_t)g_req; a[0].b.size = reqlen;
    a[1].b.addr = (u64)(uintptr_t)g_rsp; a[1].b.size = rsplen;
    a[3].b.addr = (u64)(uintptr_t)&is64; a[3].b.size = 4;
    a[4].b.addr = (u64)(uintptr_t)g_req; a[4].b.size = reqlen;
    a[5].b.addr = (u64)(uintptr_t)g_rsp; a[5].b.size = rsplen;
    a[6].o.fd = -1; a[7].o.fd = -1; a[8].o.fd = -1; a[9].o.fd = -1;
    s32 res = 0;
    int rc = smci(g_ta, 0, PACK(4, 2, 4, 0), a, &res);
    if (rc) { printf("  ioctl rc=%d (%s)\n", rc, strerror(-rc)); return -900 + rc; }
    if (res) { printf("  invoke res=%d\n", res); return -800 + res; }
    u32 st = 0; memcpy(&st, g_rsp, 4);
    return (int)st;
}

static int readf(const char *p, u8 *out, size_t cap, size_t *len) {
    FILE *f = fopen(p, "rb"); if (!f) { printf("open %s: %s\n", p, strerror(errno)); return -1; }
    size_t n = fread(out, 1, cap, f); fclose(f); *len = n; return 0;
}

static const char *envd(const char *k, const char *d) { const char *v = getenv(k); return (v && *v) ? v : d; }

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *dev   = envd("FBE_SMCI_DEV", "/dev/smcinvoke");
    const char *taname= envd("FBE_TA_NAME", "keymaster64");
    unsigned    svc   = (unsigned)strtoul(envd("FBE_TA_SVC", "122"), NULL, 0);
    const char *innerp= NULL, *blobp = NULL;
    const char *rawp  = envd("FBE_KEY_OUT", "/run/fbe/storage.key");
    const char *hexp  = envd("FBE_KEY_HEX", "/run/fbe/storage.key.hex");

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if      (!strcmp(a, "-D") && i+1 < argc) dev    = argv[++i];
        else if (!strcmp(a, "-T") && i+1 < argc) taname = argv[++i];
        else if (!strcmp(a, "-S") && i+1 < argc) svc    = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "-i") && i+1 < argc) innerp = argv[++i];
        else if (!strcmp(a, "-b") && i+1 < argc) blobp  = argv[++i];
        else if (!strcmp(a, "-o") && i+1 < argc) rawp   = argv[++i];
        else if (!strcmp(a, "-x") && i+1 < argc) hexp   = argv[++i];
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            printf("usage: kmkey [-D smcidev] [-T taname] [-S svc] [-i inner] [-b blob] [-o rawkey] [-x hexkey]\n");
            printf("  尺寸: 254=内层KMKD  282=encrypted_key(自动跳过28字节)  214=wrapped blob(需先unwrap)\n");
            return 0;
        } else { printf("unknown arg %s\n", a); return 64; }
    }
    if (!innerp && !blobp) innerp = "/run/fbe/inner.bin";
    printf("KMKEY dev=%s ta=%s svc=%u\n", dev, taname, svc);

    static u8 buf[8192]; size_t blen = 0;
    const u8 *inner; size_t ilen;
    if (blobp) {
        if (readf(blobp, buf, sizeof buf, &blen)) return 1;
        if (blen == 282 || blen == 214 + 0) {
            /* 282: 28 字节头 + 254 内层 */
            if (blen == 282) { inner = buf + 28; ilen = blen - 28; }
            else {
                printf("输入 %zu 字节：这是 keymaster 输出的 wrapped blob。\n", blen);
                printf("它必须先经 TA unwrap 得到 encrypted_key(282B)，本工具集不含这一步。\n");
                printf("在已 root 的 Android 上用同类工具或设备自身的 keymaster 服务完成该步后，\n");
                printf("把得到的 282B 文件用 -b 传进来（或去掉 28 字节头后用 -i 传 254B）。\n");
                return 3;
            }
        } else { inner = buf; ilen = blen; }
    } else {
        if (readf(innerp, buf, sizeof buf, &blen)) return 1;
        if (blen == 282) { inner = buf + 28; ilen = blen - 28; }
        else            { inner = buf;      ilen = blen; }
    }
    printf("inner KMKD len=%zu\n", ilen);
    if (ilen < 32 || ilen > 4096) { printf("尺寸不合理，拒绝\n"); return 4; }

    int root = open(dev, O_RDWR);
    if (root < 0) {
        printf("open %s: %s\n", dev, strerror(errno));
        printf("提示：老设备是 /dev/qseecom（ioctl 接口不同），本程序只实现 smcinvoke。\n");
        return 2;
    }

    int envs[2]; int ne = 0;
    { int srvfd; struct { u64 a, b; } sarg = { 4, 0 }; errno = 0;
      srvfd = ioctl(root, 0xc0109803UL, &sarg);
      printf("SERVER_REQ rc=%d errno=%s\n", srvfd, strerror(errno));
      if (srvfd >= 0) envs[ne++] = srvfd; }

    for (int e = 0; e < ne; e++) {
        union smci_arg a[3]; s32 res = 0; int rc;
        u32 uidv = svc; memset(a, 0, sizeof a);
        a[0].b.addr = (u64)(uintptr_t)&uidv; a[0].b.size = 4;
        rc = smci(envs[e], 0, PACK(1, 0, 0, 1), a, &res);
        printf("env %d: open(%u) rc=%d res=%d\n", e, svc, rc, res);
        if (rc || res) continue;
        int loader = (int)a[1].o.fd;
        u32 arch = 0; memset(a, 0, sizeof a);
        a[0].b.addr = (u64)(uintptr_t)taname; a[0].b.size = strlen(taname);
        a[1].b.addr = (u64)(uintptr_t)&arch; a[1].b.size = 4;
        rc = smci(loader, 2, PACK(1, 1, 0, 1), a, &res);
        printf("lookupTA(%s) rc=%d res=%d arch=%u\n", taname, rc, res, arch);
        if (rc || res) continue;
        g_ta = (int)a[2].o.fd;

        static u8 pl[8192];
        int n = 0;
        pl[n++] = 0xa1; pl[n++] = 0x0b; pl[n++] = 0x19; pl[n++] = 0x01; pl[n++] = 0x2c;
        printf("setVersion status=%d\n", ta_send(0x3121, pl, n));

        n = 0;
        pl[n++] = 0xa1; pl[n++] = 0x17;
        if (ilen < 24)       { pl[n++] = (u8)(0x40 | ilen); }
        else if (ilen < 256) { pl[n++] = 0x58; pl[n++] = (u8)ilen; }
        else                 { pl[n++] = 0x59; pl[n++] = (u8)(ilen >> 8); pl[n++] = (u8)ilen; }
        memcpy(pl + n, inner, ilen); n += ilen;
        int st = ta_send(0x311d, pl, n);
        printf("CONVERT status=%d\n", st);
        u32 l2 = 0; memcpy(&l2, g_rsp + 4, 4);
        u32 avail = l2;
        u8 *p = g_rsp + 8;
        for (u32 i = 0; i + 2 < avail; i++) {
            if (p[i] == 0x58 && p[i+1] == 100 && i + 2 + 100 <= avail) {
                if (rawp) { FILE *f = fopen(rawp, "wb"); if (f) { fwrite(p + i + 2, 1, 100, f); fclose(f); } }
                if (hexp) { FILE *f = fopen(hexp, "w");
                    if (f) { for (int k = 0; k < 100; k++) fprintf(f, "%02x", p[i+2+k]); fputc(10, f); fclose(f); } }
                printf("KEY 100B -> %s (hex -> %s)\n", rawp ? rawp : "-", hexp ? hexp : "-");
                return 0;
            }
        }
        printf("KEY not found (%u bytes payload)\n", avail);
        break;
    }
    return 5;
}
