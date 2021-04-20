#define pr_fmt(fmt) "cs423_mp4: " fmt

#include <linux/lsm_hooks.h>
#include <linux/security.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/binfmts.h>
#include "mp4_given.h"

#define BUFF_SIZE 512

/**
 * get_inode_sid - Get the inode mp4 security label id
 *
 * @inode: the input inode
 *
 * @return the inode's security id if found.
 *
 */
static int get_inode_sid(struct inode *inode)
{
	struct dentry *this_dentry;
	char buff[BUFF_SIZE];
	ssize_t res;

	if (!inode || !inode->i_op)
		return MP4_NO_ACCESS;

	/* let pass through if no xattr */
	if (!inode->i_op->getxattr)
		return MP4_NO_ACCESS;

	this_dentry = d_find_alias(inode);
	if (!this_dentry)
		return MP4_NO_ACCESS;
	
	res = inode->i_op->getxattr(this_dentry, XATTR_NAME_MP4, buff, BUFF_SIZE);
	/* put back dentry */
	dput(this_dentry);
	if (res < 0)
		return MP4_NO_ACCESS;

	/* NULL terminate string */
	buff[res] = '\0';

	return __cred_ctx_to_sid(buff);
}

/**
 * mp4_cred_alloc_blank - Allocate a blank mp4 security label
 *
 * @cred: the new credentials
 * @gfp: the atomicity of the memory allocation
 *
 */
static int mp4_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
	struct mp4_security *tsec;

	if (!cred)
		return -EINVAL;

	tsec = kzalloc(sizeof(struct mp4_security), gfp);
	if (!tsec)
		return -ENOMEM;

	/* init to our no access label */
	tsec->mp4_flags = MP4_NO_ACCESS;

	cred->security = tsec;

	return 0;
}

/**
 * mp4_bprm_set_creds - Set the credentials for a new task
 *
 * @bprm: The linux binary preparation structure
 *
 * returns 0 on success.
 */
static int mp4_bprm_set_creds(struct linux_binprm *bprm)
{
	int res;
	int sid;

	if (!bprm | !bprm->file | !bprm->cred)
		return 0;

	sid = get_inode_sid(bprm->file->f_inode);
	if (sid < 0)
		return sid;

	/* allocate security blob if it's NULL */
	if (bprm->cred->security == NULL) {
		res = mp4_cred_alloc_blank(bprm->cred, GFP_KERNEL);
		if (res < 0) 
			return res;
	}

	if (sid == MP4_TARGET_SID) {
		((struct mp4_security *)bprm->cred->security)->mp4_flags = MP4_TARGET_SID;
	}

	return 0;
}


/**
 * mp4_cred_free - Free a created security label
 *
 * @cred: the credentials struct
 *
 */
static void mp4_cred_free(struct cred *cred)
{
	/* assert cred->security is well defined and not NULL */
	if (!cred || !cred->security || (unsigned long) cred->security >= PAGE_SIZE)
		return;

	kfree(cred->security);
	cred->security = NULL;
}

/**
 * mp4_cred_prepare - Prepare new credentials for modification
 *
 * @new: the new credentials
 * @old: the old credentials
 * @gfp: the atomicity of the memory allocation
 *
 */
static int mp4_cred_prepare(struct cred *new, const struct cred *old,
			    gfp_t gfp)
{
	const struct mp4_security *old_tsec;
	struct mp4_security *tsec;
	int res;

	if (!new)
		return -EINVAL;
		
	mp4_cred_free(new);
	
	if (!old || !old->security) {
		/* no old credentials to copy, create blank */
		res = mp4_cred_alloc_blank(new, GFP_KERNEL);
		if (res < 0) 
			return res;
	}
	else {
		/* use old credentials */
		old_tsec = old->security;

		tsec = kmemdup(old_tsec, sizeof(struct mp4_security), gfp);
		if (!tsec)
			return -ENOMEM;

		new->security = tsec;
	}

	return 0;
}

/**
 * mp4_inode_init_security - Set the security attribute of a newly created inode
 *
 * @inode: the newly created inode
 * @dir: the containing directory
 * @qstr: unused
 * @name: where to put the attribute name
 * @value: where to put the attribute value
 * @len: where to put the length of the attribute
 *
 * returns 0 if all goes well, -ENOMEM if no memory, -EOPNOTSUPP to skip
 *
 */
static int mp4_inode_init_security(struct inode *inode, struct inode *dir,
				   const struct qstr *qstr,
				   const char **name, void **value, size_t *len)
{
	const struct mp4_security *curr_sec;
	char *name_ptr;
	char *value_ptr;

	curr_sec = current_security();

	/* check for NULL pointers */
	if (!inode || !dir || !name || !value || !curr_sec)
		return -EOPNOTSUPP;


	/* set inode as target if they were created by a target process */
	if (curr_sec->mp4_flags == MP4_TARGET_SID) {
		/* set name */
		name_ptr = kstrdup(XATTR_MP4_SUFFIX, GFP_KERNEL);
		if(!name_ptr)
			return -ENOMEM;
		*name = name_ptr;

		/* set value */
		value_ptr = kstrdup("read-write", GFP_KERNEL);
		if (!value_ptr)
			return -ENOMEM;
		*value = value_ptr;

		/* length of value */
		if (len)
			*len = 11;

		return 0;
	}
	else {
		return -EOPNOTSUPP;
	}
}

/**
 * mp4_has_permission - Check if subject has permission to an object
 *
 * @ssid: the subject's security id
 * @osid: the object's security id
 * @mask: the operation mask
 *
 * returns 0 is access granter, -EACCES otherwise
 *
 */
static int mp4_has_permission(int ssid, int osid, int mask)
{
	int op;

	/* we only look at these 4 operations */
	op = mask & (MAY_EXEC | MAY_WRITE | MAY_READ | MAY_APPEND);

	/* is program labeled with target? */
	if (ssid == MP4_TARGET_SID) { // yes
		switch (osid) {

			case MP4_NO_ACCESS:
				return -EACCES;
			
			case MP4_READ_OBJ:
				if (op & ~MAY_READ) {
					return -EACCES;
				}
				else {
					return 0;
				}

			case MP4_READ_WRITE:
				if (op & ~(MAY_READ|MAY_WRITE|MAY_APPEND)) {
					return -EACCES;
				}
				else {
					return 0;
				}
			
			case MP4_WRITE_OBJ:
				if (op & ~(MAY_WRITE|MAY_APPEND)) {
					return -EACCES;
				}
				else {
					return 0;
				}
			
			case MP4_EXEC_OBJ:
				if (op & ~(MAY_READ|MAY_EXEC)) {
					return -EACCES;
				}
				else {
					return 0;
				}
			
			case MP4_READ_DIR:
				if (op & ~(MAY_READ|MAY_EXEC)) {
					return -EACCES;
				}
				else {
					return 0;
				}
			
			case MP4_RW_DIR:
				if (op & ~(MAY_READ|MAY_WRITE|MAY_EXEC|MAY_APPEND)) {
					return -EACCES;
				}
				else {
					return 0;
				}
			
		}
	}
	else { // no
		switch (osid) {

			case MP4_NO_ACCESS:
				return 0;
			
			case MP4_READ_OBJ:
				if (op & ~MAY_READ) {
					return -EACCES;
				}
				else {
					return 0;
				}

			case MP4_READ_WRITE:
				if (op & ~MAY_READ) {
					return -EACCES;
				}
				else {
					return 0;
				}
			
			case MP4_WRITE_OBJ:
				if (op & ~MAY_READ) {
					return -EACCES;
				}
				else {
					return 0;
				}
			
			case MP4_EXEC_OBJ:
				if (op & ~(MAY_READ|MAY_EXEC)) {
					return -EACCES;
				}
				else {
					return 0;
				}

			case MP4_READ_DIR:
				return 0;

			case MP4_RW_DIR:
				return 0;
			
		}
	}
	return 0;
}

/**
 * mp4_inode_permission - Check permission for an inode being opened
 *
 * @inode: the inode in question
 * @mask: the access requested
 *
 * This is the important access check hook
 *
 * returns 0 if access is granted, -EACCES otherwise
 *
 */
static int mp4_inode_permission(struct inode *inode, int mask)
{
	struct dentry *this_dentry;
	char buff[BUFF_SIZE];
	char *path;
	const struct mp4_security *curr_sec;
	int ssid, osid;
	int res;

	if (!inode)
		return 0;

	this_dentry = d_find_alias(inode);
	if (!this_dentry)
		return 0;

	path = dentry_path_raw(this_dentry, buff, BUFF_SIZE);

	/* put back dentry */
	dput(this_dentry);

	/* skip heavily used paths to speed up boot */
	if (mp4_should_skip_path(path))
		return 0;

	curr_sec = current_security();
	if (!curr_sec)
		return 0;
	ssid = curr_sec->mp4_flags;
	osid = get_inode_sid(inode);

	res = mp4_has_permission(ssid, osid, mask);
	
	if (!res)
		if (printk_ratelimit())
			pr_info("Permission failed. ssid: %d, osid: %d, mask: %d, path: %s\n", ssid, osid, mask, path);

	return res;
}


/*
 * This is the list of hooks that we will using for our security module.
 */
static struct security_hook_list mp4_hooks[] = {
	/*
	 * inode function to assign a label and to check permission
	 */
	LSM_HOOK_INIT(inode_init_security, mp4_inode_init_security),
	LSM_HOOK_INIT(inode_permission, mp4_inode_permission),

	/*
	 * setting the credentials subjective security label when laucnhing a
	 * binary
	 */
	LSM_HOOK_INIT(bprm_set_creds, mp4_bprm_set_creds),

	/* credentials handling and preparation */
	LSM_HOOK_INIT(cred_alloc_blank, mp4_cred_alloc_blank),
	LSM_HOOK_INIT(cred_free, mp4_cred_free),
	LSM_HOOK_INIT(cred_prepare, mp4_cred_prepare)
};

static __init int mp4_init(void)
{
	/*
	 * check if mp4 lsm is enabled with boot parameters
	 */
	if (!security_module_enable("mp4"))
		return 0;

	pr_info("mp4 LSM initializing..");

	/*
	 * Register the mp4 hooks with lsm
	 */
	security_add_hooks(mp4_hooks, ARRAY_SIZE(mp4_hooks));

	return 0;
}

/*
 * early registration with the kernel
 */
security_initcall(mp4_init);
