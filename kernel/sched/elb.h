#ifndef _KERNEL_SCHED_ELB_H
#define _KERNEL_SCHED_ELB_H

#include <linux/sched.h>

struct elb_sysfs_data {
	unsigned int enabled;
	unsigned int up_threshold;
	unsigned int down_threshold;
	unsigned int reserve_core;
	unsigned int reserved_core_id;
	unsigned int saturation_threshold;
	unsigned int packing_enable;
	unsigned int packing_limit;
};

extern struct elb_sysfs_data elb_data;

/**
 * elb_select_cpu - Custom load balancer select_cpu hook
 * @p: The task that is waking up
 * @prev_cpu: The CPU the task last ran on
 * @sd_flag: The scheduling domain flag (e.g. SD_BALANCE_WAKE)
 * @wake_flags: WF_SYNC etc
 *
 * Returns: Target CPU ID, or -1 to let standard CFS handle it.
 */
int elb_select_cpu(struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags);

#endif /* _KERNEL_SCHED_ELB_H */
