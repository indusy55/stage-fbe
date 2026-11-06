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

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    unsigned svc = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 122;
    const char *taname = argc > 2 ? argv[2] : "keymaster64";
    const char *innerp = argc > 3 ? argv[3] : "/data/local/tmp/inner.bin";
    const char *outp = argc > 4 ? argv[4] : "/data/local/tmp/eph_static.bin";
    printf("KMKEY svc=%u ta=%s inner=%s\n", svc, taname, innerp);

    static u8 inner[8192]; size_t ilen = 0;
    if (readf(innerp, inner, sizeof inner, &ilen)) return 1;
    printf("inner blob len=%zu\n", ilen);

    int root = open("/dev/smcinvoke", O_RDWR);
    if (root < 0) { printf("open smcinvoke: %s\n", strerror(errno)); return 2; }
    struct { u64 a, b; } sarg = { 4, 0 };
    errno = 0;
    int srvfd = ioctl(root, 0xc0109803UL, &sarg);
    printf("SERVER_REQ rc=%d errno=%s\n", srvfd, strerror(errno));

    int envs[2]; int ne = 0;
    { union smci_arg a[3]; s32 res = 0; memset(a, 0, sizeof a);
      int rc = smci(root, 1, PACK(1, 0, 0, 1), a, &res);
      printf("op1 env rc=%d res=%d fd=%lld\n", rc, res, (long long)a[1].o.fd);
      if (!rc && !res) envs[ne++] = (int)a[1].o.fd; }
    if (srvfd >= 0) envs[ne++] = srvfd;

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
        printf("lookupTA rc=%d res=%d arch=%u\n", rc, res, arch);
        if (rc || res) continue;
        g_ta = (int)a[2].o.fd;
        printf("TA object fd=%d\n", g_ta);

        /* setVersion */
        static u8 pl[4096];
        int n = 0;
        pl[n++] = 0xa1; pl[n++] = 0x0b; pl[n++] = 0x19; pl[n++] = 0x01; pl[n++] = 0x2c;  /* {11: 300} */
        printf("setVersion status=%d\n", ta_send(0x3121, pl, n));

        /* convertStorageKeyToEphemeral: {23: bstr(inner)} */
        n = 0;
        pl[n++] = 0xa1; pl[n++] = 0x17;
        if (ilen < 24) { pl[n++] = (u8)(0x40 | ilen); }
        else if (ilen < 256) { pl[n++] = 0x58; pl[n++] = (u8)ilen; }
        else { pl[n++] = 0x59; pl[n++] = (u8)(ilen >> 8); pl[n++] = (u8)ilen; }
        memcpy(pl + n, inner, ilen); n += ilen;
        printf("convert payload len=%d\n", n);
        int st = ta_send(0x311d, pl, n);
        printf("CONVERT status=%d\n", st);
        u32 l2 = 0; memcpy(&l2, g_rsp + 4, 4);
        printf("rsp hdr=%08x payload_len=%u\n", st, l2);
        u32 show = l2 < 400 ? l2 : 400;
        printf("rsp:");
        for (u32 i = 0; i < show; i++) printf(" %02x", g_rsp[8 + i]);
        printf("\n");
        /* extract first bstr of length 100 from payload */
        u8 *p = g_rsp + 8; u32 avail = l2;
        int found = 0;
        for (u32 i = 0; i + 2 < avail; i++) {
            if (p[i] == 0x58 && p[i+1] == 100 && i + 2 + 100 <= avail) {
                FILE *f = fopen(outp, "wb");
                if (f) { fwrite(p + i + 2, 1, 100, f); fclose(f); }
                printf("KEY found at off %u -> %s\n", i + 2, outp);
                for (int k = 0; k < 16; k++) printf("%02x", p[i+2+k]);
                printf("\n");
                found = 1; break;
            }
        }
        if (!found) printf("KEY not found (%u bytes)\n", avail);
        break;
    }
    return 0;
}
