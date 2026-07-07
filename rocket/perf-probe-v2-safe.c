// SPDX-License-Identifier: GPL-2.0-only
/*
 * rocket-patches/perf-probe-v2-safe.c  —  REFERENCE SNIPPET (not built directly)
 *
 * The v2 / SAFE replacement for the perf-counter code in rocket_drv.c. This is
 * the canonical source for the next `086-rocket-drv-perf-counters.patch` regen; it
 * supersedes v1 (which read the 0x2000 amount page and HARD-LOCKED the SoC).
 *
 * What changed v1 -> v2:
 *   - REMOVED entirely: the `counters` read (readl of the 0x2000 page) and the
 *     `clear` write (0x2210/0x2410). The 0x2000 page is undecoded on rk3588 and
 *     an MMIO read there aborts the bus. We remove rather than stub it: a stub is
 *     a re-armable footgun; removal + this comment keeps the lesson, not the gun.
 *   - ADDED: a SAFE, read-only `ddma` probe of the 0x8000 DDMA block, which IS a
 *     defined/decoded domain in the Mesa rocket register map (e.g. 0x8030 =
 *     DDMA CFG_STATUS). Goal: characterize the rk3588 DDMA block and see whether
 *     the legacy rk356x amount offsets
 *     (0x8034/38/3c) carry anything counter-like on rk3588.
 *
 * Safety model (defense in depth — this probe still touches an address rocket
 * does not normally map, so treat it as load-bearing):
 *   1. READ-ONLY. No writes to the DDMA page => cannot corrupt DMA config
 *      (note 0x8010 = RD_WEIGHT_1 is a real config reg; the old "clear" would
 *      have written there — another reason the v1 clear is gone).
 *   2. CORE-0 ONLY. One core's page, fewer risky reads.
 *   3. DISARMED BY DEFAULT. The MMIO reads happen only if the module is loaded
 *      with `rocket_ddma_probe=1`. A plain `cat` while disarmed prints a notice
 *      and touches no hardware, so nothing can trip it accidentally.
 *   4. KNOWN-REGISTER-FIRST. CFG_STATUS (0x8030, a defined reg) is read before
 *      anything else, each read in its own seq_printf (sequential, so if it
 *      aborts we know exactly which read did it — unlike v1's unsequenced trio).
 *   5. Operator must run with a HARDWARE WATCHDOG + serial console armed, so if
 *      even 0x8030 fails to decode the box auto-recovers and the hang is logged.
 *
 * Integration (the other two edits stay identical to v1):
 *   - includes: + <linux/debugfs.h> <linux/io.h> <linux/seq_file.h> <linux/uaccess.h>
 *   - rocket_register(): call rocket_perf_init();  rocket_unregister(): rocket_perf_fini();
 * Insert the block below between the rocket_driver struct `};` and rocket_register().
 */

/* ===== (v2) SAFE read-only DDMA register probe (debugfs) ================== */
#define ROCKET_DDMA_PAGE_OFF	0x8000		/* DDMA domain base within a core window */
#define ROCKET_DDMA_PAGE_SZ	0x1000
/* page-relative offsets (absolute = 0x8000 + these; names per Mesa registers.xml) */
#define DDMA_CFG_OUTSTANDING	0x000		/* 0x8000 */
#define DDMA_RD_WEIGHT_0	0x004		/* 0x8004 */
#define DDMA_WR_WEIGHT_0	0x008		/* 0x8008 */
#define DDMA_CFG_ID_ERROR	0x00c		/* 0x800c */
#define DDMA_RD_WEIGHT_1	0x010		/* 0x8010 (config — never written) */
#define DDMA_CFG_STATUS		0x030		/* 0x8030 — KNOWN reg, read FIRST */
#define DDMA_LEGACY_DT_WR	0x034		/* 0x8034 (rk356x old DT_WR_AMOUNT) */
#define DDMA_LEGACY_DT_RD	0x038		/* 0x8038 (rk356x old DT_RD_AMOUNT) */
#define DDMA_LEGACY_WT_RD	0x03c		/* 0x803c (rk356x old WT_RD_AMOUNT) */

static bool rocket_ddma_probe;
module_param(rocket_ddma_probe, bool, 0644);
MODULE_PARM_DESC(rocket_ddma_probe,
	"SAFE probe: 1 => /sys/kernel/debug/rocket_perf/ddma reads core-0 DDMA regs (READ-ONLY). Default 0 (disarmed). Run with a hardware watchdog.");

static struct dentry *rocket_perf_dir;
static bool rocket_perf_held;

static int rocket_ddma_show(struct seq_file *m, void *unused)
{
	struct platform_device *pdev;
	struct resource *res;
	struct device *dev;
	void __iomem *io;
	int err;

	if (!rocket_ddma_probe) {
		seq_puts(m, "disarmed (read-only DDMA @0x8000 probe). Load with rocket_ddma_probe=1 to enable;\n");
		seq_puts(m, "run a hardware watchdog + serial console first. v1 0x2000 counter read was removed\n");
		seq_puts(m, "(it hard-locked the SoC).\n");
		return 0;
	}
	if (!rdev || rdev->num_cores < 1 || !rdev->cores[0].dev) {
		seq_puts(m, "no device probed\n");
		return 0;
	}

	dev = rdev->cores[0].dev;
	pdev = to_platform_device(dev);
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pc");
	if (!res) {
		seq_puts(m, "no \"pc\" resource\n");
		return 0;
	}

	io = ioremap(res->start + ROCKET_DDMA_PAGE_OFF, ROCKET_DDMA_PAGE_SZ);
	if (!io) {
		seq_puts(m, "ioremap failed\n");
		return 0;
	}

	err = pm_runtime_resume_and_get(dev);
	if (err) {
		seq_printf(m, "pm_runtime resume failed (%d)\n", err);
		iounmap(io);
		return 0;
	}

	seq_printf(m, "core0 DDMA @ pc_base+0x%x (phys 0x%llx), read-only:\n",
		   ROCKET_DDMA_PAGE_OFF,
		   (unsigned long long)res->start + ROCKET_DDMA_PAGE_OFF);

	/* KNOWN register first: if the 0x8000 page decodes, this returns; if not,
	 * the abort is on this single identified read. */
	seq_printf(m, "  0x8030 CFG_STATUS      = 0x%08x\n", readl(io + DDMA_CFG_STATUS));
	seq_printf(m, "  0x8000 CFG_OUTSTANDING = 0x%08x\n", readl(io + DDMA_CFG_OUTSTANDING));
	seq_printf(m, "  0x8004 RD_WEIGHT_0     = 0x%08x\n", readl(io + DDMA_RD_WEIGHT_0));
	seq_printf(m, "  0x8008 WR_WEIGHT_0     = 0x%08x\n", readl(io + DDMA_WR_WEIGHT_0));
	seq_printf(m, "  0x800c CFG_ID_ERROR    = 0x%08x\n", readl(io + DDMA_CFG_ID_ERROR));
	seq_printf(m, "  0x8010 RD_WEIGHT_1     = 0x%08x\n", readl(io + DDMA_RD_WEIGHT_1));
	seq_printf(m, "  0x8034 legacy DT_WR    = 0x%08x\n", readl(io + DDMA_LEGACY_DT_WR));
	seq_printf(m, "  0x8038 legacy DT_RD    = 0x%08x\n", readl(io + DDMA_LEGACY_DT_RD));
	seq_printf(m, "  0x803c legacy WT_RD    = 0x%08x\n", readl(io + DDMA_LEGACY_WT_RD));

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	iounmap(io);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(rocket_ddma);

/*
 * power_hold: pin all core power domains ON (pm_runtime_get_sync, no put) so the
 * DDMA regs can be read before/after an externally-run job (e.g. to see whether
 * the legacy offsets change with traffic); '0' releases. Keeping a domain
 * powered is what an in-flight job does, so this is safe.
 */
static ssize_t rocket_perf_hold_write(struct file *file, const char __user *ubuf,
				      size_t len, loff_t *off)
{
	unsigned int core;
	char c = 0;
	bool on;

	if (!rdev)
		return -ENODEV;
	if (len == 0 || get_user(c, ubuf))
		return -EFAULT;
	on = (c != '0');

	if (on == rocket_perf_held)		/* idempotent: never double get/put */
		return len;

	for (core = 0; core < rdev->num_cores; core++) {
		struct device *dev = rdev->cores[core].dev;

		if (on)
			pm_runtime_get_sync(dev);
		else
			pm_runtime_put(dev);
	}
	rocket_perf_held = on;
	return len;
}

static const struct file_operations rocket_perf_hold_fops = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.write	= rocket_perf_hold_write,
	.llseek	= default_llseek,
};

static void rocket_perf_init(void)
{
	rocket_perf_dir = debugfs_create_dir("rocket_perf", NULL);
	debugfs_create_file("ddma", 0400, rocket_perf_dir, NULL, &rocket_ddma_fops);
	debugfs_create_file("power_hold", 0200, rocket_perf_dir, NULL, &rocket_perf_hold_fops);
}

static void rocket_perf_fini(void)
{
	if (rocket_perf_held && rdev) {
		unsigned int core;

		for (core = 0; core < rdev->num_cores; core++)
			pm_runtime_put(rdev->cores[core].dev);
		rocket_perf_held = false;
	}
	debugfs_remove_recursive(rocket_perf_dir);
	rocket_perf_dir = NULL;
}
