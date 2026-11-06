/* stage7raw4: unlock userdata in initramfs, move Debian root to /, boot systemd */
typedef unsigned long u64;
static long sc(long n, long a, long b, long c2, long d, long e) {
    register long x8 asm("x8") = n;
    register long x0 asm("x0") = a; register long x1 asm("x1") = b;
    register long x2 asm("x2") = c2; register long x3 asm("x3") = d; register long x4 asm("x4") = e;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4) : "memory");
    return x0;
}
void *memcpy(void *d, const void *s, unsigned long n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c2, unsigned long n) { char *a = d; while (n--) *a++ = (char)c2; return d; }
static char lb[400]; static int ln; static char ob[600];
static long logfd2 = -1; static long kmfd = -1; static long bcfd = -1;
static char klogbuf[262144];
static void a(const char *s) { while (*s && ln < 380) lb[ln++] = *s++; }
static void an(long v) { char t[24]; int n = 0; if (v < 0) { a("-"); v = -v; } if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } while (n) { char x[2] = { t[--n], 0 }; a(x); } }
static void em(void) {
    int i = 0; const char *p = "S7G "; while (*p) ob[i++] = *p++;
    for (int k = 0; k < ln && i < 560; k++) ob[i++] = lb[k];
    ob[i++] = 0x0a;
    if (kmfd < 0) kmfd = sc(56, -100, (long)"/dev/kmsg", 1, 0, 0);
    if (kmfd >= 0) sc(64, kmfd, (long)ob, i, 0, 0);
    if (logfd2 >= 0) { sc(64, logfd2, (long)ob, i, 0, 0); sc(82, logfd2, 0, 0, 0, 0); }
    ln = 0;
}
static void bc(long code, long v1, long v2) {
    if (bcfd < 0) bcfd = sc(56, -100, (long)"/dev/sda12", 2, 0, 0);
    if (bcfd < 0) return;
    char t[80]; int i = 0;
    const char *p = "BC"; while (*p) t[i++] = *p++;
    long vs[3]; vs[0] = code; vs[1] = v1; vs[2] = v2;
    for (int k = 0; k < 3; k++) {
        long v = vs[k];
        if (v < 0) { t[i++] = '-'; v = -v; }
        char d[14]; int n = 0; if (!v) d[n++] = '0';
        while (v) { d[n++] = (char)('0' + v % 10); v /= 10; }
        while (n) t[i++] = d[--n];
        t[i++] = (k == 2) ? '\n' : ' ';
    }
    static char area[4096];
    long rn = sc(67, bcfd, (long)area, 4096, 0x100000L, 0);
    long slot = 0;
    if (rn > 0) { long q = 0; while (q < rn && area[q]) q++; slot = q; }
    if (slot + i < 4000) {
        sc(68, bcfd, (long)t, i, 0x100000L + slot, 0);
        sc(82, bcfd, 0, 0, 0, 0);
    }
}
static u64 *kbuf;
static long insmod_file(const char *path) {
    long fd = sc(56, -100, (long)path, 0, 0, 0);
    if (fd < 0) return -1000 + fd;
    long rn = sc(63, fd, (long)kbuf, 12L * 1024 * 1024, 0, 0);
    sc(57, fd, 0, 0, 0, 0);
    if (rn <= 0) return -999;
    return sc(105, (long)kbuf, rn, (long)"", 0, 0);
}
static int exists(const char *p) { long f = sc(56, -100, (long)p, 0, 0, 0); if (f >= 0) { sc(57, f, 0, 0, 0, 0); return 1; } return 0; }
static long mkfile(const char *p) { return sc(56, -100, (long)p, 0x241, 0644, 0); }
static long writeall(long f, const char *s, long n) { long o = 0; while (o < n) { long w = sc(64, f, (long)(s + o), n - o, 0, 0); if (w <= 0) return -1; o += w; } return o; }
static void sleepms(long ms) { struct { long s, ns; } t; t.s = ms / 1000; t.ns = (ms % 1000) * 1000000L; sc(101, (long)&t, 0, 0, 0, 0); }
static long run(const char *path, char *const argv[]) {
    long p = sc(220, 17, 0, 0, 0, 0);
    if (p == 0) { sc(221, (long)path, (long)argv, 0, 0, 0); sc(93, 127, 0, 0, 0, 0); }
    if (p < 0) return p;
    int st = 0; sc(260, p, (long)&st, 0, 0, 0);
    return st;
}
static int readsmall(const char *p, char *out, int cap) {
    long f = sc(56, -100, (long)p, 0, 0, 0);
    if (f < 0) return -1;
    long n = sc(63, f, (long)out, cap, 0, 0);
    sc(57, f, 0, 0, 0, 0);
    return (int)n;
}
static int bcb_state(void) {
    char b[16];
    long f = sc(56, -100, (long)"/dev/sda12", 0, 0, 0);
    if (f < 0) return -1;
    long n = sc(63, f, (long)b, 13, 0, 0);
    sc(57, f, 0, 0, 0, 0);
    if (n < 13) return 0;
    const char *t = "boot-recovery";
    for (int i = 0; i < 13; i++) if (b[i] != t[i]) return 0;
    return 1;
}
static long bcb_set_recovery(void) {
    long f = sc(56, -100, (long)"/dev/sda12", 2, 0, 0);
    if (f < 0) return f;
    long w = sc(68, f, (long)"boot-recovery", 13, 0, 0);
    sc(82, f, 0, 0, 0, 0);
    sc(57, f, 0, 0, 0, 0);
    return w;
}
static void ksnap(const char *path) {
    long n = sc(116, 3, (long)klogbuf, (long)sizeof klogbuf - 1, 0, 0);
    if (n <= 0) { a("ksnap syslog="); an(n); em(); return; }
    long f = mkfile(path);
    if (f < 0) return;
    writeall(f, klogbuf, n);
    sc(57, f, 0, 0, 0, 0);
}
static const char *modlist[] = {
    "xm_power.ko", "sched-penalty.ko", "sg.ko", "rtc-pm8xxx.ko", "rt_mod.ko", "spmi-pmic-arb.ko", "qti_thermal_vendor_hooks.ko", "qti-regmap-debugfs.ko",
    "qnoc-qos.ko", "qcom_cpucp.ko", "qcom-reboot-reason.ko", "qcom-ipcc.ko", "qcom_hwspinlock.ko", "qcom-cpufreq-thermal.ko", "qcom-cpufreq-hw.ko", "qcom_cpu_vendor_hooks.ko",
    "printk_enhance.ko", "phy-qcom-ufs.ko", "qcom_scmi_vendor.ko", "perf_actuator.ko", "nvmem_qcom-spmi-sdam.ko", "zsmalloc.ko", "msm_dma_iommu_mapping.ko", "millet_oem_cgroup.ko",
    "miev.ko", "qcom_scmi_client.ko", "mi_ubt.ko", "mi_rmap_efficiency.ko", "lz4asm.ko", "llcc-qcom.ko", "kshrink_slabd.ko", "ipclite.ko",
    "iommu-logger.ko", "io_monitor.ko", "icc-debug.ko", "smem.ko", "gic_intr_routing.ko", "gh_msgq.ko", "gh_dbl.ko", "proxy-consumer.ko",
    "dynamic_readahead.ko", "debug_symbol.ko", "debug-regulator.ko", "cqhci.ko", "cpu_phys_log_map.ko", "cpu_hotplug.ko", "plat_common.ko", "cmd-db.ko",
    "boottime.ko", "bootinfo.ko", "millet_core.ko", "gh_arm_drv.ko", "vip_sched.ko", "xr_qi.ko", "gh_rm_drv.ko", "qcom-scm.ko",
    "secure_buffer.ko", "mem_buf_dev.ko", "qcom_dma_heaps.ko", "minidump.ko", "qcom_ipc_logging.ko", "crm-v2.ko", "gdsc-regulator.ko", "clk-qcom.ko",
    "socinfo.ko", "rtg.ko", "qcom_llcc_pmu.ko", "qcom-pmu-lib.ko", "qcom_rpmh.ko", "dcvs_fp.ko", "qcom-dcvs.ko", "mist.ko",
    "sched-walt.ko", "msm_performance.ko", "zram.ko", "stub-regulator.ko", "thermal_pause.ko", "videocc-sun.ko", "rpmh-regulator.ko", "tcsrcc-sun.ko",
    "qrtr.ko", "qcom_tsens.ko", "tcsrcc-tuna.ko", "qcom-pdc.ko", "xr_qs.ko", "qcom_logbuf_vendor_hooks.ko", "qcom_logbuf_boot_log.ko", "qcom_ice.ko",
    "qcom-dload-mode.ko", "pmic-pon-log.ko", "phy-qcom-ufs-qrbtc-sdm845.ko", "phy-qcom-ufs-qmp-v4-sun.ko", "phy-qcom-ufs-qmp-v4-niobe.ko", "perf_helper.ko", "node_cache.ko", "ntsync.ko",
    "qcom_aoss.ko", "millet_sig.ko", "millet_pkg.ko", "millet_hs.ko", "mi_wq_dynamic_priority.ko", "pinctrl-msm.ko", "memory_dump_v2.ko", "mi_game.ko",
    "mem_buf_msgq.ko", "icc-bcm-voter.ko", "hwid.ko", "mdt_loader.ko", "gpucc-tuna.ko", "gpucc-sun.ko", "gpucc-kera.ko", "qcom_wdt_core.ko",
    "gcc-tuna.ko", "gcc-sun.ko", "gcc-kera.ko", "evacc-sun.ko", "dispcc-tuna.ko", "dispcc-sun.ko", "debug_ext.ko", "dcc_v2.ko",
    "cpq.ko", "cpucp_fast.ko", "clk-rpmh.ko", "clk-dummy.ko", "camcc-tuna.ko", "camcc-sun.ko", "camcc-kera.ko", "cambistmclkcc-tuna.ko",
    "cambistmclkcc-sun.ko", "binder_gki.ko", "bcl_pmic5.ko", "qcom_iommu_util.ko", "pinctrl-sun.ko", "millet_binder.ko", "ufshcd-crypto-qti.ko", "msm_qmp.ko",
    "pinctrl-kera.ko", "pinctrl-tuna.ko", "sdhci-msm.ko", "mi_power.ko", "mem_buf.ko", "icc-rpmh.ko", "gunyah_loader.ko", "gh_virt_wdt.ko",
    "arm_smmu.ko", "qrtr-gunyah.ko", "qcom-spmi-pmic.ko", "qnoc-sun.ko", "ufs_qcom.ko", "qnoc-tuna.ko", "qnoc-kera.ko", "mi_memory.ko",
    "tz_log_dlkm.ko", "tmecom-intf.ko", "qseecom_proxy.ko", "si_core_module.ko", "mem_object.ko", "smcinvoke_dlkm.ko",
    0
};

static void ksnap2(const char *path) {
    long n = sc(116, 3, (long)klogbuf, (long)sizeof klogbuf - 1, 0, 0);
    if (n <= 0) return;
    long f = mkfile(path);
    if (f < 0) return;
    writeall(f, klogbuf, n);
    sc(57, f, 0, 0, 0, 0);
}
static int udc_configured(void) {
    char b[40];
    int n = readsmall("/newroot/debian2/sys/class/udc/a600000.dwc3/state", b, 31);
    if (n < 4) return 0;
    return (b[0] == 'c' && b[1] == 'o' && b[2] == 'n' && b[3] == 'f');
}
static void supline(long tf, long sec, const char *tag) {
    if (tf < 0) return;
    char line[128]; int q = 0;
    const char *p = "t="; while (*p) line[q++] = *p++;
    { char d[8]; int n = 0; long v = sec; if (!v) d[n++] = '0';
      while (v) { d[n++] = (char)('0' + v % 10); v /= 10; }
      char e[8]; int m = 0; while (n) e[m++] = d[--n]; while (m) line[q++] = e[--m]; }
    const char *p2 = " pid1="; while (*p2) line[q++] = *p2++;
    char c[32]; int n = readsmall("/proc/1/comm", c, 24);
    if (n > 0) { for (int k = 0; k < n && c[k] != 10; k++) line[q++] = c[k]; }
    else { const char *nn = "none"; while (*nn) line[q++] = *nn++; }
    const char *p3 = " tag="; while (*p3) line[q++] = *p3++;
    while (*tag) line[q++] = *tag++;
    const char *p4 = " sd="; while (*p4) line[q++] = *p4++;
    line[q++] = exists("/newroot/debian2/run/systemd") ? '1' : '0';
    line[q++] = 10;
    sc(64, tf, (long)line, q, 0, 0);
    sc(82, tf, 0, 0, 0, 0);
}
static void sup(void) {
    long tf = sc(56, -100, (long)"/newroot/stage7-tick.log", 0x241, 0644, 0);
    int ok = 0;
    for (int i = 1; i <= 20; i++) {
        sleepms(250);
        { char nm[64]; int q = 0; const char *pre = "/newroot/stage7-kf-";
          while (*pre) nm[q++] = *pre++;
          if (i >= 10) nm[q++] = (char)('0' + i / 10);
          nm[q++] = (char)('0' + i % 10);
          nm[q++] = '.'; nm[q++] = 'l'; nm[q++] = 'o'; nm[q++] = 'g'; nm[q] = 0;
          ksnap2(nm); }
        if (udc_configured()) { ok = 1; break; }
    }
    for (int i = 1; i <= 15; i++) {
        sleepms(1000);
        { char nm[64]; int q = 0; const char *pre = "/newroot/stage7-k1s-";
          while (*pre) nm[q++] = *pre++;
          if (i >= 10) nm[q++] = (char)('0' + i / 10);
          nm[q++] = (char)('0' + i % 10);
          nm[q++] = '.'; nm[q++] = 'l'; nm[q++] = 'o'; nm[q++] = 'g'; nm[q] = 0;
          ksnap2(nm); }
        supline(tf, i, "1s");
        if (udc_configured()) { ok = 1; break; }
    }
    if (!ok) {
        for (int i = 15; i < 150; i += 5) {
            sleepms(5000);
            ksnap2("/newroot/stage7-kmsg-last.log");
            supline(tf, i + 5, "5s");
            if (udc_configured()) { ok = 1; break; }
        }
    }
    long f = mkfile("/newroot/stage7-sup.log");
    if (f >= 0) {
        writeall(f, ok ? "usb_configured=1\n" : "usb_configured=0\n", 17);
        { char c[64]; int n = readsmall("/proc/1/comm", c, 32);
          if (n > 0) { writeall(f, "pid1_comm=", 10); writeall(f, c, n); writeall(f, "\n", 1); }
          else writeall(f, "pid1_comm=<none>\n", 17); }
        writeall(f, exists("/newroot/debian2/run/systemd") ? "run_systemd=1\n" : "run_systemd=0\n", 14);
        sc(82, f, 0, 0, 0, 0); sc(57, f, 0, 0, 0, 0);
    }
    if (ok) { for (;;) sleepms(30000); }
    for (int i = 0; i < 8; i++) {
        long r = sc(142, 0xfee1dead, 672274793, 0x1234567, 0, 0);
        long sf = sc(56, -100, (long)"/proc/sysrq-trigger", 1, 0, 0);
        if (sf >= 0) { sc(64, sf, (long)"b", 1, 0, 0); sc(57, sf, 0, 0, 0, 0); }
        sleepms(2000);
    }
    for (;;) sc(93, 0, 0, 0, 0, 0);
}
static const char *mtdlist[] = { "mtd.ko", "block2mtd.ko", "mtd_blkdevs.ko", "mtdblock.ko", "mtdoops.ko", 0 };
static const char *usblist[] = {
    "phy-generic.ko", "qmp_dlkm.ko", "phy-msm-ssusb-qmp.ko", "phy-msm-snps-eusb2.ko", "phy-msm-m31-eusb2.ko",
    "repeater.ko", "qmi_helpers.ko", "smem.ko", "qcom_smd.ko",
    "msm_dma_iommu_mapping.ko", "gh_dbl.ko", "gh_msgq.ko", "gh_arm_drv.ko",
    "gh_rm_drv.ko", "qcom-scm.ko", "secure_buffer.ko", "mem_buf_dev.ko",
    "debug_symbol.ko", "qcom_dma_heaps.ko", "minidump.ko", "qcom_ipc_logging.ko",
    "qcom_glink.ko", "qcom_glink_smem.ko", "rproc_qcom_common.ko", "pdr_interface.ko",
    "qti_pmic_glink.ko", "ucsi_qti_glink.ko", "qti-regmap-debugfs.ko", "socinfo.ko",
    "hwid.ko", "miev.ko", "wcd_usbss_i2c.ko", "redriver.ko",
    "mca_sysfs.ko", "debug-regulator.ko", "proxy-consumer.ko", "gdsc-regulator.ko",
    "crm-v2.ko", "clk-qcom.ko", "usb_f_rndis.ko",
    "usb_f_gsi.ko", "usb_f_qdss.ko", "usb_f_ccid.ko", "usb_f_cdev.ko",
    "f_fs_ipc_log.ko",
    0
};
static int load_set(const char *const *list, int passes) {
    int last = 0;
    for (int pass = 0; pass < passes; pass++) {
        int pok = 0;
        for (int i = 0; list[i]; i++) {
            char path[300]; int p = 0; const char *pre = "/lib/modules/";
            while (*pre) path[p++] = *pre++;
            const char *nm = list[i]; while (*nm && p < 290) path[p++] = *nm++;
            path[p] = 0;
            long r = insmod_file(path);
            if (r == -1002) { p = 0; path[p++] = '/'; nm = list[i]; while (*nm && p < 290) path[p++] = *nm++; path[p] = 0; r = insmod_file(path); }
            if (r == 0) pok++;
        }
        a("set pass"); an(pass); a(" ok="); an(pok); em();
        if (list == usblist) { for (int i = 0; list[i]; i++) { char p2[300]; int q2 = 0; const char *pr = "/lib/modules/";
            while (*pr) p2[q2++] = *pr++; const char *nm2 = list[i]; while (*nm2) p2[q2++] = *nm2++; p2[q2] = 0;
            long rc = insmod_file(p2); a("  "); a(list[i]); a("="); an(rc); em(); } }
        last = pok;
        if (!pok) break;
    }
    return last;
}
static char keybuf[128];
void _start(void) {
    sc(34, -100, (long)"/dev", 0755, 0, 0);
    sc(34, -100, (long)"/proc", 0755, 0, 0);
    sc(34, -100, (long)"/sys", 0755, 0, 0);
    sc(34, -100, (long)"/newroot", 0755, 0, 0);
    sc(40, (long)"devtmpfs", (long)"/dev", (long)"devtmpfs", 0, 0);
    sc(40, (long)"proc", (long)"/proc", (long)"proc", 0, 0);
    sc(40, (long)"sysfs", (long)"/sys", (long)"sysfs", 0, 0);
    sc(34, -100, (long)"/sys/kernel/config", 0755, 0, 0);
    { long cf = sc(40, (long)"configfs", (long)"/sys/kernel/config", (long)"configfs", 0, 0);
      a("configfs="); an(cf); }
    kmfd = sc(56, -100, (long)"/dev/kmsg", 1, 0, 0);
    bcfd = sc(56, -100, (long)"/dev/sda12", 2, 0, 0);
    bc(1, kmfd, bcfd); em();
    kbuf = (u64*)sc(222, 0, 12L * 1024 * 1024, 3, 0x22, -1);
    a("p102 start kbuf="); an((long)kbuf); em();
    { int pok = load_set(modlist, 2); a("main modules newok="); an(pok); em(); bc(2, pok, 0); }
    { int w = 0; for (; w < 60; w++) { if (exists("/dev/sda34")) break; sleepms(200); }
      a("sda34 wait="); an(w); a(" present="); an(exists("/dev/sda34")); a(" smc="); an(exists("/dev/smcinvoke")); em();
      bc(3, exists("/dev/sda34"), exists("/dev/smcinvoke")); }
    { int bs = bcb_state(); a("bcb="); an(bs); em(); bc(4, bs, 0);
      if (bs == 1) { a("bcb recovery -> stock init"); em(); goto fallback; } }
    if (!(exists("/dev/mapper/control") && exists("/inner.bin") && exists("/kmkey"))) {
        a("missing kmkey/inner/control"); em(); goto fallback; }
    { char *ka[] = { "kmkey", "122", "keymaster64", "/inner.bin", "/key.bin", 0 };
      long st = run("/kmkey", ka);
      a("kmkey st="); an(st); em(); bc(5, st, 0); }
    { int n = readsmall("/key.bin", keybuf, 100);
      a("key read="); an(n); em(); bc(6, n, 0);
      if (n != 100) goto fallback;
      char hx[256]; int h = 0; const char *dig = "0123456789abcdef";
      for (int i = 0; i < 100; i++) { hx[h++] = dig[(keybuf[i] >> 4) & 15]; hx[h++] = dig[keybuf[i] & 15]; }
      hx[h] = 0;
      char *da[] = { "dmcreate", "userdata", "0", "484286424", "default-key", "aes-xts-plain64", hx,
                     "0", "/dev/sda34", "0", "3", "sector_size:4096", "iv_large_sectors", "wrappedkey_v0", 0 };
      long ds = run("/dmcreate", da);
      a("dmcreate st="); an(ds); em(); bc(7, ds, 0);
      long mr = sc(40, (long)"/dev/mapper/userdata", (long)"/newroot", (long)"f2fs", 0, 0);
      a("mount data="); an(mr); em(); bc(8, mr, 0);
      if (mr != 0) goto fallback;
      logfd2 = sc(56, -100, (long)"/newroot/stage7-boot.log", 0x441, 0644, 0);
      a("logfd="); an(logfd2); em(); }
    { /* the kernel firmware loader resolves /lib/firmware in the INITRAMFS root, */
      /* so bind the Debian firmware tree there before handing over.            */
      sc(34, -100, (long)"/lib", 0755, 0, 0);
      sc(34, -100, (long)"/lib/firmware", 0755, 0, 0);
      sc(34, -100, (long)"/newroot/debian2/lib/firmware", 0755, 0, 0);
      long fb = sc(40, (long)"/newroot/debian2/lib/firmware", (long)"/lib/firmware", 0, 0x1000, 0);
      a("firmware bind="); an(fb); a(" adsp.mdt="); an(exists("/lib/firmware/adsp.mdt")); em(); bc(20, fb, 0); }
    if (!exists("/newroot/debian2/sbin/init")) { a("no debian init"); em(); goto fallback; }
    { long bw = bcb_set_recovery(); a("bcb set="); an(bw); em(); bc(9, bw, 0); }
    { int up = load_set(usblist, 3); a("usb modules newok="); an(up);
      a(" udc="); an(exists("/sys/class/udc/a600000.dwc3")); em(); bc(10, up, exists("/sys/class/udc/a600000.dwc3")); }
    { int mo = load_set(mtdlist, 3); a("mtd modules newok="); an(mo); em();
      { char mb[512]; int n = readsmall("/proc/mtd", mb, 500);
        if (n > 0) { long f = mkfile("/newroot/stage7-mtd.txt"); if (f >= 0) { writeall(f, mb, n); sc(57, f, 0, 0, 0, 0); } }
        a("proc_mtd len="); an(n); em(); } }
    ksnap2("/newroot/stage7-kmsg-usb.log");
    a("udc after usb="); an(exists("/sys/class/udc/a600000.dwc3")); em();
    { /* extra module list supplied at runtime via /data/stage7-extra.txt */
      long f = sc(56, -100, (long)"/newroot/stage7-extra.txt", 0, 0, 0);
      if (f >= 0) {
          static char mb[8192]; long n = sc(63, f, (long)mb, 8000, 0, 0); sc(57, f, 0, 0, 0, 0);
          if (n > 0) {
              int mods = 0, ok = 0;
              for (int pass = 0; pass < 3; pass++) {
                  int i = 0, pok = 0;
                  while (i < n) {
                      int j = i; while (j < n && mb[j] != 10 && mb[j] != 13) j++;
                      int len = j - i;
                      if (len > 3) {
                          char path[300]; int p = 0; const char *pre = "/lib/modules/";
                          while (*pre) path[p++] = *pre++;
                          for (int k = 0; k < len && p < 290; k++) path[p++] = mb[i + k];
                          path[p] = 0;
                          long r = insmod_file(path);
                          if (r == 0) pok++;
                      }
                      while (j < n && (mb[j] == 10 || mb[j] == 13)) j++;
                      i = j;
                  }
                  a("extra pass"); an(pass); a(" ok="); an(pok); em();
                  mods = pok; if (!pok) break;
                  ok += pok;
              }
              a("extra total="); an(ok); em();
          }
      }
      sleepms(3000);
      int up2 = load_set(usblist, 2); a("usb retry newok="); an(up2);
      a(" udc="); an(exists("/sys/class/udc/a600000.dwc3")); em(); }
    a("debian attempt"); em();
    sc(34, -100, (long)"/newroot/debian2/dev", 0755, 0, 0);
    sc(34, -100, (long)"/newroot/debian2/proc", 0755, 0, 0);
    sc(34, -100, (long)"/newroot/debian2/sys", 0755, 0, 0);
    { long b1 = sc(40, (long)"/dev", (long)"/newroot/debian2/dev", 0, 0x5000, 0);
      long b2 = sc(40, (long)"/proc", (long)"/newroot/debian2/proc", 0, 0x5000, 0);
      long b3 = sc(40, (long)"/sys", (long)"/newroot/debian2/sys", 0, 0x5000, 0);
      long b4 = sc(40, (long)"/newroot/debian2", (long)"/newroot/debian2", 0, 0x5000, 0);
      a("binds="); an(b1); a("/"); an(b2); a("/"); an(b3); a(" self="); an(b4); em(); bc(11, b1, b4);
      if (b4 != 0) goto fallback; }
    { long sp = sc(220, 17, 0, 0, 0, 0);
      if (sp == 0) sup();
      a("supervisor="); an(sp); em(); bc(12, sp, 0); }
    { sc(34, -100, (long)"/dev/pts", 0755, 0, 0);
      long dpt = sc(40, (long)"devpts", (long)"/dev/pts", (long)"devpts", 0, (long)"mode=0620,ptmxmode=0666");
      long mfd = sc(56, -100, (long)"/dev/pts/ptmx", 2, 0, 0);
      a("devpts="); an(dpt); a(" ptmx="); an(mfd); em();
      if (mfd >= 0) {
          int zero = 0, num = 0;
          sc(29, mfd, 0x40045431, (long)&zero, 0, 0);
          long gr = sc(29, mfd, 0x80045430, (long)&num, 0, 0);
          char sp[40]; int q = 0; const char *pre = "/dev/pts/";
          while (*pre) sp[q++] = *pre++;
          char dg[8]; int dn = 0; long v = num; if (!v) dg[dn++] = '0';
          while (v) { dg[dn++] = (char)('0' + v % 10); v /= 10; }
          char rv[8]; int rn = 0; while (dn) rv[rn++] = dg[--dn]; while (rn) sp[q++] = rv[--rn];
          sp[q] = 0;
          sc(33, -100, (long)"/newroot/debian2/dev/console", 0x21b6, 0x501, 0);
          long bc = sc(40, (long)sp, (long)"/newroot/debian2/dev/console", 0, 0x1000, 0);
          a("pty num="); an(num); a(" bindcon="); an(bc); em();
          long cp2 = sc(220, 17, 0, 0, 0, 0);
          if (cp2 == 0) {
              char cbuf[2048]; long lf = sc(56, -100, (long)"/newroot/stage7-console.log", 0x241, 0644, 0);
              for (int i = 0; i < 3000; i++) {
                  long rn2 = sc(63, mfd, (long)cbuf, 2000, 0, 0);
                  if (rn2 <= 0) { struct { long s, ns; } t2 = { 0, 20000000L }; sc(101, (long)&t2, 0, 0, 0, 0); continue; }
                  if (lf >= 0) { sc(64, lf, (long)cbuf, rn2, 0, 0); sc(82, lf, 0, 0, 0, 0); }
              }
              for (;;) sc(93, 0, 0, 0, 0, 0);
          }
      } }
    { long ur = sc(97, 0x20000000, 0, 0, 0, 0);
      a("unshare="); an(ur); em(); bc(14, ur, 0); }
    { long cp = sc(220, 17, 0, 0, 0, 0);
      if (cp == 0) {
          long un2 = sc(97, 0x20000, 0, 0, 0, 0);
          long pv = sc(40, 0, (long)"/", 0, 0x40000 | 0x4000, 0);
          long mproc = sc(40, (long)"proc", (long)"/proc", (long)"proc", 0, 0);
          a("child unshare_mnt="); an(un2); a(" proc="); an(mproc); em();
          long b1 = sc(40, (long)"/dev", (long)"/newroot/debian2/dev", 0, 0x1000, 0);
          long b2 = sc(40, (long)"/proc", (long)"/newroot/debian2/proc", 0, 0x1000, 0);
          long b3 = sc(40, (long)"/sys", (long)"/newroot/debian2/sys", 0, 0x1000, 0);
          long b4 = sc(40, (long)"/newroot/debian2", (long)"/newroot/debian2", 0, 0x5000, 0);
          a("child private="); an(pv); a(" binds="); an(b1); a("/"); an(b2); a("/"); an(b3); a(" self="); an(b4); em();
          sc(49, (long)"/newroot/debian2", 0, 0, 0, 0);
          long mv = sc(40, (long)"/newroot/debian2", (long)"/", 0, 0x2000, 0);
          long ch = sc(51, (long)".", 0, 0, 0, 0);
          a("child ms_move="); an(mv); a(" chroot="); an(ch); em();
          sc(49, (long)"/", 0, 0, 0, 0);
          { long f = sc(56, -100, (long)"/proc/sys/kernel/panic_on_oops", 1, 0, 0);
            if (f >= 0) { sc(64, f, (long)"0", 1, 0, 0); sc(57, f, 0, 0, 0, 0); } }
          a("child exec debian init"); em();
          char *ia[] = { "/sbin/init", 0 };
          char *ie[] = { "PATH=/usr/sbin:/usr/bin:/sbin:/bin", "HOME=/root", 0 };
          sc(221, (long)"/sbin/init", (long)ia, (long)ie, 0, 0);
          a("child exec failed"); em();
          sc(93, 127, 0, 0, 0, 0);
      }
      a("inner_pid="); an(cp); em(); }
    { long f = sc(56, -100, (long)"/proc/sys/kernel/panic_on_oops", 1, 0, 0);
      if (f >= 0) { sc(64, f, (long)"0", 1, 0, 0); sc(57, f, 0, 0, 0, 0); }
      char pb[4]; int pn = readsmall("/proc/sys/kernel/panic_on_oops", pb, 3);
      a("panic_on_oops="); an(pn); a(":"); if (pn > 0) { char tt[2] = { pb[0], 0 }; a(tt); } em(); }
    { for (int i = 0; i < 60; i++) { sleepms(5000); ksnap2("/newroot/stage7-kmsg-last.log"); } }
    a("watch done"); em();
    if (udc_configured()) {
        a("usb configured -> stay in linux"); em();
        for (;;) { sleepms(30000); ksnap2("/newroot/stage7-kmsg-last.log"); }
    }
    a("usb down -> android"); em();
fallback:
    a("FAIL -> android"); em();
    if (logfd2 >= 0) sc(57, logfd2, 0, 0, 0, 0);
    sc(39, (long)"/newroot/debian2/dev", 2, 0, 0, 0);
    sc(39, (long)"/newroot/debian2/proc", 2, 0, 0, 0);
    sc(39, (long)"/newroot/debian2/sys", 2, 0, 0, 0);
    sc(39, (long)"/newroot", 2, 0, 0, 0);
    sc(39, (long)"/sys", 2, 0, 0, 0);
    sc(39, (long)"/proc", 2, 0, 0, 0);
    sc(39, (long)"/dev", 2, 0, 0, 0);
    char *a1[] = { "/init.ksu", 0 }; char *ep[] = { "PATH=/sbin:/bin", 0 };
    sc(221, (long)"/init.ksu", (long)a1, (long)ep, 0, 0);
    sc(221, (long)"/init.real", (long)a1, (long)ep, 0, 0);
    for (;;) sc(93, 0, 0, 0, 0, 0);
}