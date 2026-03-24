/*
 * ekernel_thermal.c - E-Kernel 8895 thermal profiles driver
 *
 * Copyright (C) 2026 E-Kernel Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Provides three thermal profiles (Balanced/Gaming/Cool) with runtime
 * switching via /proc/ekernel/thermal_profile and boot parameter
 * ekernel.thermal_profile=0/1/2.
 *
 * Also exposes /proc/ekernel/thermal_stats for current thermal zone
 * temperatures and active trip point values.
 *
 * SAFETY: Hard ceiling of 108C is enforced on all passive trip points.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/uaccess.h>

#include "../thermal_core.h"

#define EKERNEL_THERMAL_VERSION		"1.0"

/* Thermal profile IDs */
#define PROFILE_BALANCED	0
#define PROFILE_GAMING		1
#define PROFILE_COOL		2
#define PROFILE_MAX		2

/* Safety ceiling - no passive trip point may exceed this. */
#define EKERNEL_TEMP_CEILING_MC	108000
#define EKERNEL_TEMP_MIN_MC	60000

/*
 * Profile temperature offsets (in millicelsius) applied to the
 * base (Balanced) passive trip points.
 */
#define GAMING_PASSIVE_OFFSET_MC	5000
#define COOL_PASSIVE_OFFSET_MC		(-5000)

static int ekernel_thermal_profile = PROFILE_BALANCED;
static struct proc_dir_entry *ekernel_proc_dir;
static DEFINE_MUTEX(ekernel_thermal_lock);

struct ekernel_thermal_zone {
	char name[THERMAL_NAME_LENGTH];
	int ntrips;
	int base_temps[THERMAL_MAX_TRIPS];
	enum thermal_trip_type trip_types[THERMAL_MAX_TRIPS];
};

static struct ekernel_thermal_zone *ekernel_zones;
static int ekernel_zone_count;

static const char * const profile_names[] = {
	[PROFILE_BALANCED]	= "Balanced",
	[PROFILE_GAMING]	= "Gaming",
	[PROFILE_COOL]		= "Cool",
};

static inline int ekernel_clamp_temp(int temp_mc)
{
	if (temp_mc > EKERNEL_TEMP_CEILING_MC)
		return EKERNEL_TEMP_CEILING_MC;
	if (temp_mc < EKERNEL_TEMP_MIN_MC)
		return EKERNEL_TEMP_MIN_MC;
	return temp_mc;
}

static int ekernel_get_profile_offset(void)
{
	switch (ekernel_thermal_profile) {
	case PROFILE_GAMING:
		return GAMING_PASSIVE_OFFSET_MC;
	case PROFILE_COOL:
		return COOL_PASSIVE_OFFSET_MC;
	case PROFILE_BALANCED:
	default:
		return 0;
	}
}

static const char *ekernel_trip_type_name(enum thermal_trip_type type)
{
	switch (type) {
	case THERMAL_TRIP_PASSIVE:
		return "passive";
	case THERMAL_TRIP_ACTIVE:
		return "active";
	case THERMAL_TRIP_HOT:
		return "hot";
	case THERMAL_TRIP_CRITICAL:
		return "critical";
	default:
		return "unknown";
	}
}

static int ekernel_cache_thermal_zones_locked(void)
{
	struct device_node *thermal_zones, *child;
	int count = 0;
	int idx = 0;
	int ret = -ENODEV;

	if (ekernel_zone_count)
		return 0;

	kfree(ekernel_zones);
	ekernel_zones = NULL;

	thermal_zones = of_find_node_by_name(NULL, "thermal-zones");
	if (!thermal_zones)
		return -ENODEV;

	for_each_child_of_node(thermal_zones, child)
		count++;

	if (!count)
		goto out_put_zones;

	ekernel_zones = kcalloc(count, sizeof(*ekernel_zones), GFP_KERNEL);
	if (!ekernel_zones) {
		ret = -ENOMEM;
		goto out_put_zones;
	}

	for_each_child_of_node(thermal_zones, child) {
		struct thermal_zone_device *tz;
		const struct thermal_trip *trips;
		int j;
		int ntrips;

		tz = thermal_zone_get_zone_by_name(child->name);
		if (IS_ERR(tz))
			continue;

		ntrips = of_thermal_get_ntrips(tz);
		trips = of_thermal_get_trip_points(tz);
		if (ntrips <= 0 || !trips)
			continue;

		ekernel_zones[idx].ntrips = min(ntrips, THERMAL_MAX_TRIPS);
		strlcpy(ekernel_zones[idx].name, child->name,
			sizeof(ekernel_zones[idx].name));

		for (j = 0; j < ekernel_zones[idx].ntrips; j++) {
			ekernel_zones[idx].base_temps[j] = trips[j].temperature;
			ekernel_zones[idx].trip_types[j] = trips[j].type;
		}

		idx++;
	}

	ekernel_zone_count = idx;
	if (!ekernel_zone_count) {
		kfree(ekernel_zones);
		ekernel_zones = NULL;
		goto out_put_zones;
	}

	ret = 0;

out_put_zones:
	of_node_put(thermal_zones);
	return ret;
}

static int ekernel_apply_profile_locked(void)
{
	int offset = ekernel_get_profile_offset();
	int i;
	int j;
	int ret;
	int first_err = 0;

	ret = ekernel_cache_thermal_zones_locked();
	if (ret)
		return ret;

	for (i = 0; i < ekernel_zone_count; i++) {
		struct thermal_zone_device *tz;
		bool updated = false;

		tz = thermal_zone_get_zone_by_name(ekernel_zones[i].name);
		if (IS_ERR(tz) || !tz->ops || !tz->ops->set_trip_temp)
			continue;

		for (j = 0; j < ekernel_zones[i].ntrips; j++) {
			int trip_temp;

			if (ekernel_zones[i].trip_types[j] != THERMAL_TRIP_PASSIVE)
				continue;

			trip_temp = ekernel_clamp_temp(
				ekernel_zones[i].base_temps[j] + offset);
			ret = tz->ops->set_trip_temp(tz, j, trip_temp);
			if (ret) {
				if (!first_err)
					first_err = ret;
				continue;
			}

			updated = true;
		}

		if (updated)
			thermal_zone_device_update(tz);
	}

	return first_err;
}

static int thermal_profile_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d (%s)\n",
		   ekernel_thermal_profile,
		   profile_names[ekernel_thermal_profile]);
	return 0;
}

static int thermal_profile_open(struct inode *inode, struct file *file)
{
	return single_open(file, thermal_profile_show, NULL);
}

static ssize_t thermal_profile_write(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *ppos)
{
	char buf[4];
	int profile;
	int ret;
	size_t len = min(count, sizeof(buf) - 1);

	if (copy_from_user(buf, buffer, len))
		return -EFAULT;
	buf[len] = '\0';

	if (kstrtoint(buf, 10, &profile))
		return -EINVAL;

	if (profile < 0 || profile > PROFILE_MAX)
		return -EINVAL;

	mutex_lock(&ekernel_thermal_lock);
	ekernel_thermal_profile = profile;
	ret = ekernel_apply_profile_locked();
	mutex_unlock(&ekernel_thermal_lock);

	if (ret)
		pr_warn("ekernel_thermal: failed to apply profile %d: %d\n",
			profile, ret);

	pr_info("ekernel_thermal: profile set to %d (%s)\n",
		profile, profile_names[profile]);

	return count;
}

static const struct file_operations thermal_profile_fops = {
	.owner		= THIS_MODULE,
	.open		= thermal_profile_open,
	.read		= seq_read,
	.write		= thermal_profile_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int thermal_stats_show(struct seq_file *m, void *v)
{
	int i;
	int ret;

	seq_printf(m, "E-Kernel 8895 Thermal Stats (Profile: %s)\n",
		   profile_names[ekernel_thermal_profile]);
	seq_puts(m, "================================================================\n");
	seq_printf(m, "%-20s %-10s %-10s %-10s\n",
		   "Zone", "Temp(C)", "Trip(C)", "Type");
	seq_puts(m, "----------------------------------------------------------------\n");

	mutex_lock(&ekernel_thermal_lock);
	ret = ekernel_cache_thermal_zones_locked();
	if (ret) {
		seq_puts(m, "No thermal zones available\n");
		goto out_unlock;
	}

	for (i = 0; i < ekernel_zone_count; i++) {
		struct thermal_zone_device *tz;
		int temp;
		int j;

		tz = thermal_zone_get_zone_by_name(ekernel_zones[i].name);
		if (IS_ERR(tz))
			continue;

		ret = thermal_zone_get_temp(tz, &temp);
		if (ret)
			temp = THERMAL_TEMP_INVALID;
		else
			temp /= 1000;

		for (j = 0; j < ekernel_zones[i].ntrips; j++) {
			int trip_temp;
			enum thermal_trip_type type;
			char temp_buf[16];
			char trip_buf[16];

			if (tz->ops && tz->ops->get_trip_temp &&
			    !tz->ops->get_trip_temp(tz, j, &trip_temp)) {
				snprintf(trip_buf, sizeof(trip_buf), "%d",
					 trip_temp / 1000);
			} else {
				strlcpy(trip_buf, "N/A", sizeof(trip_buf));
			}

			if (!tz->ops || !tz->ops->get_trip_type ||
			    tz->ops->get_trip_type(tz, j, &type))
				type = ekernel_zones[i].trip_types[j];

			if (j == 0) {
				if (temp == THERMAL_TEMP_INVALID)
					strlcpy(temp_buf, "N/A",
						sizeof(temp_buf));
				else
					snprintf(temp_buf, sizeof(temp_buf), "%d",
						 temp);
			} else {
				temp_buf[0] = '\0';
			}

			seq_printf(m, "%-20s %-10s %-10s %-10s\n",
				   (j == 0) ? ekernel_zones[i].name : "",
				   temp_buf, trip_buf,
				   ekernel_trip_type_name(type));
		}
	}

	seq_puts(m, "================================================================\n");
	seq_printf(m, "Safety ceiling: %dC (hard limit)\n",
		   EKERNEL_TEMP_CEILING_MC / 1000);

out_unlock:
	mutex_unlock(&ekernel_thermal_lock);
	return 0;
}

static int thermal_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, thermal_stats_show, NULL);
}

static const struct file_operations thermal_stats_fops = {
	.owner		= THIS_MODULE,
	.open		= thermal_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init ekernel_thermal_profile_setup(char *str)
{
	int val;

	if (kstrtoint(str, 10, &val) == 0) {
		if (val >= 0 && val <= PROFILE_MAX)
			ekernel_thermal_profile = val;
	}
	return 1;
}
__setup("ekernel.thermal_profile=", ekernel_thermal_profile_setup);

static int __init ekernel_thermal_init(void)
{
	int ret;

	pr_info("ekernel_thermal: initializing v%s\n", EKERNEL_THERMAL_VERSION);

	ekernel_proc_dir = proc_mkdir("ekernel", NULL);
	if (!ekernel_proc_dir) {
		pr_err("ekernel_thermal: failed to create /proc/ekernel\n");
		return -ENOMEM;
	}

	if (!proc_create("thermal_profile", 0664,
			 ekernel_proc_dir, &thermal_profile_fops)) {
		pr_err("ekernel_thermal: failed to create thermal_profile\n");
		goto err_remove_dir;
	}

	if (!proc_create("thermal_stats", 0444,
			 ekernel_proc_dir, &thermal_stats_fops)) {
		pr_err("ekernel_thermal: failed to create thermal_stats\n");
		goto err_remove_profile;
	}

	mutex_lock(&ekernel_thermal_lock);
	ret = ekernel_apply_profile_locked();
	mutex_unlock(&ekernel_thermal_lock);
	if (ret)
		pr_warn("ekernel_thermal: thermal zones not ready yet: %d\n", ret);

	pr_info("ekernel_thermal: profile=%d (%s), ceiling=%dC\n",
		ekernel_thermal_profile,
		profile_names[ekernel_thermal_profile],
		EKERNEL_TEMP_CEILING_MC / 1000);

	return 0;

err_remove_profile:
	remove_proc_entry("thermal_profile", ekernel_proc_dir);
err_remove_dir:
	remove_proc_entry("ekernel", NULL);
	return -ENOMEM;
}

static void __exit ekernel_thermal_exit(void)
{
	remove_proc_entry("thermal_stats", ekernel_proc_dir);
	remove_proc_entry("thermal_profile", ekernel_proc_dir);
	remove_proc_entry("ekernel", NULL);
	kfree(ekernel_zones);
	ekernel_zones = NULL;
	ekernel_zone_count = 0;
	pr_info("ekernel_thermal: unloaded\n");
}

late_initcall(ekernel_thermal_init);
module_exit(ekernel_thermal_exit);

MODULE_DESCRIPTION("E-Kernel 8895 thermal profiles driver");
MODULE_AUTHOR("E-Kernel Team");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(EKERNEL_THERMAL_VERSION);
