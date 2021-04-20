For MP4 Part 2, there were 8 functions I developed:
* I implemented mp4_cred_alloc_blank by allocating memory for the security label, initializing to zero, setting to default label.
* I implemented mp4_cred_free by using kfree to free this same memory passed in.
* For mp4_cred_prepare, I cleared the destination security label, then either copied the source label in, or default initialized the label if theres no source.
* I implemented the helper function get_inode_sid by using the inode arg to get a dentry, to get its xattrin a buffer, to convert this buffer to a int. For mp4_bprm_set_creds, =
* I used get_inode_sid to get the security label of the new task, default initialize a security blob for the task, and set the label as a target if the inode was also marked as target.
* I implemented mp4_inode_init_security by checking if the current task is marked as target, if so, then allocating xattr to mark the new file as "read-write". 
* For mp4_has_permission, I extract exec, write, read, and append requests from the input mask, and put it through a conditional matrix with the input security labels to see if it should be granted access.
* For mp4_inode_permission, I first get the dentry from the input inode, then the path from the dentry, and use the path of this input inode to check if it should be skipped to speed up boot. Then, I get the current task's and the target object's security labels, pass them into mp4_has_permission, log if access is denied, and return the result.

In all of these functions I also check for bad values or args along the way.


For MP4 Part 4, I did a couple things to determine what files need what permissions:
* Generated a dummy user to mess with.
* Ran `sudo strace -e trace=open -o output.txt /usr/bin/passwd dummy` to write a list of file accesses to output.txt
* Gave unique files and folders the appropriate permissions listed
* Ran passwd.perm, `sudo strace -e trace=open -o output.txt /usr/bin/passwd dummy`, and `cat output.txt | grep "Permission denied"` to test and update appropriately if not working
* In the script, wrote a part to modify /etc/pam.d/common-password to silence Kerberos password prompts