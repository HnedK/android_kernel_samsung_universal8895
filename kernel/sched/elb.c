#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/cpuset.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/file.h>
#include "sched.h"
#include "elb.h"

struct elb_sysfs_data elb_data = {
	.enabled = 1,
	.up_threshold = 700,
	.down_threshold = 256,
	.reserve_core = 1,
	.reserved_core_id = 7,
	.saturation_threshold = 3,
	.packing_enable = 1,
	.packing_limit = 2
};

struct kobject *elb_kobj;
DEFINE_SPINLOCK(elb_lock);

/* Sysfs Show/Store macros */
#define ELB_ATTR(name) \
	static ssize_t name##_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) \
	{ return sprintf(buf, "%u\n", elb_data.name); } \
	static ssize_t name##_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) \
	{ \
		unsigned int val; \
		if (kstrtouint(buf, 10, &val) < 0) return -EINVAL; \
		spin_lock(&elb_lock); \
		elb_data.name = val; \
		spin_unlock(&elb_lock); \
		return count; \
	} \
	static struct kobj_attribute name##_attribute = __ATTR(name, 0644, name##_show, name##_store)

ELB_ATTR(enabled);
ELB_ATTR(up_threshold);
ELB_ATTR(down_threshold);
ELB_ATTR(reserve_core);
ELB_ATTR(reserved_core_id);
ELB_ATTR(saturation_threshold);
ELB_ATTR(packing_enable);
ELB_ATTR(packing_limit);

static struct attribute *elb_attrs[] = {
	&enabled_attribute.attr,
	&up_threshold_attribute.attr,
	&down_threshold_attribute.attr,
	&reserve_core_attribute.attr,
	&reserved_core_id_attribute.attr,
	&saturation_threshold_attribute.attr,
	&packing_enable_attribute.attr,
	&packing_limit_attribute.attr,
	NULL,
};

static struct attribute_group elb_attr_group = {
	.attrs = elb_attrs,
};

int elb_select_cpu(struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags)
{
	unsigned int util = p->se.avg.util_avg;
	int target_cpu = prev_cpu;
	int cpu;
	int packed_target = -1;
	int min_rq = 999;
	bool fallback_allowed = false;
	unsigned int local_enabled, local_up, local_down, local_reserve, local_res_id, local_sat, local_pack, local_pack_limit;

	spin_lock(&elb_lock);
	local_enabled = elb_data.enabled;
	local_up = elb_data.up_threshold;
	local_down = elb_data.down_threshold;
	local_reserve = elb_data.reserve_core;
	local_res_id = elb_data.reserved_core_id;
	local_sat = elb_data.saturation_threshold;
	local_pack = elb_data.packing_enable;
	local_pack_limit = elb_data.packing_limit;
	spin_unlock(&elb_lock);

	if (!local_enabled)
		return -1; // Fallback to completely standard CFS

	if (util < local_down) {
		/* Light task logic - place on LITTLE cores */
		if (local_pack) {
			for_each_online_cpu(cpu) {
				if (cpu > 3) continue; // Only 0-3
				if (cpu_rq(cpu)->nr_running < local_pack_limit) {
					packed_target = cpu;
					break;
				}
			}
			if (packed_target != -1) {
				return packed_target;
			}
		}
		// Fallback to least loaded LITTLE
		for_each_online_cpu(cpu) {
			if (cpu > 3) continue;
			if (cpu_rq(cpu)->nr_running < min_rq) {
				min_rq = cpu_rq(cpu)->nr_running;
				target_cpu = cpu;
			}
		}
		return target_cpu;
	} else if (util >= local_up) {
		/* Heavy task logic - move to BIG cores */
		min_rq = 999;
		fallback_allowed = true;

		// Check if non-reserved BIG cores are saturated
		for_each_online_cpu(cpu) {
			if (cpu < 4) continue;
			if (local_reserve && cpu == local_res_id) continue;
			if (cpu_rq(cpu)->nr_running < local_sat) {
				fallback_allowed = false; // There is at least one non-saturated BIG core
			}
			if (cpu_rq(cpu)->nr_running < min_rq) {
				min_rq = cpu_rq(cpu)->nr_running;
				target_cpu = cpu;
			}
		}

		// If we are allowed to use the reserved core because everything else is saturated
		if (local_reserve && fallback_allowed && cpu_online(local_res_id)) {
			// Compare reserved core with the least loaded non-reserved BIG core
			if (cpu_rq(local_res_id)->nr_running < min_rq) {
				target_cpu = local_res_id;
			}
		}
		
		// If our target cpu is somehow still a LITTLE core (e.g. all bigs offline), target_cpu might be prev_cpu
        // But the loop above at least initializes min_rq over online BIGs.
        if (target_cpu < 4) {
             for_each_online_cpu(cpu) {
			    if (cpu < 4) continue;
                target_cpu = cpu;
                break;
             }
        }
        
		return target_cpu;
	}

	/* Mid range tasks: Let standard CFS handle it (return -1) */
	return -1;
}

int __init elb_init(void)
{
	int ret;
	struct kobject *ekernel_kobj;

	/* Create ekernel directory under /sys/kernel if it doesn't exist */
	ekernel_kobj = kobject_create_and_add("ekernel", kernel_kobj);
	if (!ekernel_kobj)
		return -ENOMEM;

	/* Create elb directory under /sys/kernel/ekernel */
	elb_kobj = kobject_create_and_add("elb", ekernel_kobj);
	if (!elb_kobj) {
		kobject_put(ekernel_kobj);
		return -ENOMEM;
	}

	ret = sysfs_create_group(elb_kobj, &elb_attr_group);
	if (ret) {
		kobject_put(elb_kobj);
		kobject_put(ekernel_kobj);
	}

	return ret;
}

// Do not use pure_initcall because ekernel requires kobject infrastructure
device_initcall(elb_init);
