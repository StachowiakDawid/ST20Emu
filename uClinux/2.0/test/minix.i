# 1 "inode.c"
 








# 1 "../../include/linux/module.h" 1
 



















 




 


 



 


struct kernel_sym {  
	unsigned long value;		 
	char name[60 ];	 
};

struct module_ref {
	struct module *module;
	struct module_ref *next;
};

struct internal_symbol {
	void *addr;
	const char *name;
	};

struct symbol_table {  
	int size;  
	int n_symbols;
	int n_refs;
	struct internal_symbol symbol[0];  
	struct module_ref ref[0];  
};
 



struct module {
	struct module *next;
	struct module_ref *ref;	 
	struct symbol_table *symtab;
	const char *name;
	int size;			 
	void* addr;			 
	int state;
	void (*cleanup)(void);		 
};

struct mod_routines {
	int (*init)(void);		 
	void (*cleanup)(void);		 
};

 



 



# 102 "../../include/linux/module.h"








 
extern int register_symtab_from(struct symbol_table *, long *);
extern void *get_module_symbol(char *, char *);







# 10 "inode.c" 2


# 1 "../../include/linux/sched.h" 1



 







 


  

# 1 "../../include/asm/param.h" 1



# 1 "../../include/linux/config.h" 1



# 1 "../../include/linux/autoconf.h" 1
 










 









 






 









 





 




# 4 "../../include/linux/config.h" 2


 


















 




 

 





 





# 4 "../../include/asm/param.h" 2



















# 17 "../../include/linux/sched.h" 2


extern unsigned long event;

# 1 "../../include/linux/binfmts.h" 1



# 1 "../../include/linux/ptrace.h" 1


 
 

 

















# 1 "../../include/asm/ptrace.h" 1



 





struct pt_regs {
	long		orig_nr;	 
	long		arg[5];		 
	long		result;		 
	unsigned long	wptr;		 
	unsigned long	iptr;		 
	unsigned long	status;		 
};

 











 







extern void show_regs(struct pt_regs *);




# 24 "../../include/linux/ptrace.h" 2



# 4 "../../include/linux/binfmts.h" 2


 






 


struct linux_binprm{
	char buf[128];



	char ** envp, **argv;

	unsigned long p;
	int sh_bang;
	struct inode * inode;
	int e_uid, e_gid;
	int argc, envc;
	char * filename;	 
	unsigned long loader, exec;
	int dont_iput;		 
};

 



struct linux_binfmt {
	struct linux_binfmt * next;
	long *use_count;
	int (*load_binary)(struct linux_binprm *, struct  pt_regs * regs);
	int (*load_shlib)(int fd);
	int (*core_dump)(long signr, struct pt_regs * regs);
};

extern int register_binfmt(struct linux_binfmt *);
extern int unregister_binfmt(struct linux_binfmt *);

extern int read_exec(struct inode *inode, unsigned long offset,
	char * addr, unsigned long count, int to_kmem);

extern int open_inode(struct inode * inode, int mode);

extern int init_flat_binfmt(void);
extern int init_elf_binfmt(void);
extern int init_aout_binfmt(void);
extern int init_script_binfmt(void);
extern int init_java_binfmt(void);

extern int prepare_binprm(struct linux_binprm *);
extern void remove_arg_zero(struct linux_binprm *);
extern int search_binary_handler(struct linux_binprm *,struct pt_regs *);
extern int flush_old_exec(struct linux_binprm * bprm);
extern unsigned long setup_arg_pages(unsigned long p, struct linux_binprm * bprm);




extern char **copy_strings(int argc,char ** argv);


 



# 21 "../../include/linux/sched.h" 2

# 1 "../../include/linux/personality.h" 1



# 1 "../../include/linux/linkage.h" 1









# 18 "../../include/linux/linkage.h"











# 37 "../../include/linux/linkage.h"









# 54 "../../include/linux/linkage.h"





# 68 "../../include/linux/linkage.h"



# 4 "../../include/linux/personality.h" 2




 





 














 
typedef void (*lcall7_func)(struct pt_regs *);


 




struct exec_domain {
	const char *name;
	lcall7_func handler;
	unsigned char pers_low, pers_high;
	unsigned long * signal_map;
	unsigned long * signal_invmap;
	long *use_count;
	struct exec_domain *next;
};

extern struct exec_domain default_exec_domain;

extern struct exec_domain *lookup_exec_domain(unsigned long personality);
extern int register_exec_domain(struct exec_domain *it);
extern int unregister_exec_domain(struct exec_domain *it);
  int sys_personality(unsigned long personality);


# 22 "../../include/linux/sched.h" 2

# 1 "../../include/linux/tasks.h" 1



 


 












# 23 "../../include/linux/sched.h" 2

# 1 "../../include/linux/kernel.h" 1



 





# 1 "/work/tools/lib/gcc-lib/t450/2.7.2-t800.12/include/stdarg.h" 1 3
 











































 






typedef void *__gnuc_va_list;



 



 














void va_end (__gnuc_va_list);		 


 



 

























 
 













# 137 "/work/tools/lib/gcc-lib/t450/2.7.2-t800.12/include/stdarg.h" 3


 




 

 

 

typedef __gnuc_va_list va_list;
























# 10 "../../include/linux/kernel.h" 2



 























extern void math_error(void);
  void panic(const char * fmt, ...)
	__attribute__ ((noreturn,  format (printf, 1, 2)));
  void do_exit(long error_code)
	__attribute__((noreturn)) ;
extern unsigned long simple_strtoul(const char *,char **,unsigned int);
extern int sprintf(char * buf, const char * fmt, ...);
extern int vsprintf(char *buf, const char *, va_list);

extern int session_of_pgrp(int pgrp);

extern int kill_proc(int pid, int sig, int priv);
extern int kill_pg(int pgrp, int sig, int priv);
extern int kill_sl(int sess, int sig, int priv);

  int printk(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));












 





 


  









struct sysinfo {
	long uptime;			 
	unsigned long loads[3];		 
	unsigned long totalram;		 
	unsigned long freeram;		 
	unsigned long sharedram;	 
	unsigned long bufferram;	 
	unsigned long totalswap;	 
	unsigned long freeswap;		 
	unsigned short procs;		 
	char _f[22];			 
};


# 24 "../../include/linux/sched.h" 2


# 1 "../../include/asm/system.h" 1





# 1 "../../include/asm/segment.h" 1



 
 














 







 




extern int bad_user_access_length(void);



static inline void __put_user(unsigned long x, void * y, int size)
{
	switch (size) {
		case 1:
			*(unsigned char *)y = x;
			break;
		case 2:
			*(unsigned short *)y = x;
			break;
		case 4:
			*(unsigned long *)y = x;
			break;
		default:
			bad_user_access_length();
	}
}

static inline unsigned long __get_user(const void * y, int size)
{
	switch (size) {
		case 1:
		  return *(unsigned char *)y;
		case 2:
		  return *(unsigned short *)y;
		case 4:
		  return *(unsigned long *)y;
		default:
			return bad_user_access_length();
	}
}


 













# 115 "../../include/asm/segment.h"


static inline void memcpy_fromfs(void *to,void *from,int n)
{
         if (from < to) {
	         (char *)to=(char *)to+n-1;
	         (char *)from=(char *)from+n-1;
                 for (;n>0;n--)
                         *((char *)to)--=*((char *)from)--;
         } else {
                 for (;n>0;n--)
		         *((char *)to)++=*((char *)from)++;
	 }
}
                       
static inline void memcpy_tofs(void *to,void *from,int n)
{
         if (from < to) {
	         (char *)to=(char *)to+n-1;
	         (char *)from=(char *)from+n-1;
                 for (;n>0;n--)
                         *((char *)to)--=*((char *)from)--;
         } else {
                 for (;n>0;n--)
		         *((char *)to)++=*((char *)from)++;
         }
}

 



static inline unsigned long get_fs(void)
{

	return ((1) ) ;






}

static inline unsigned long get_ds(void)
{
     
    return ((5) ) ;
}

static inline void set_fs(unsigned long val)
{





}




# 6 "../../include/asm/system.h" 2


 






static inline unsigned long __st20_gintdis(unsigned long mask)
{
	unsigned long old;
	__asm__ __volatile__ ("gintdis" : "=a" (old) : "aP" (mask) : "memory");
	return old;
}

static inline unsigned long __st20_gintenb(unsigned long mask)
{
	unsigned long old;
	__asm__ __volatile__ ("gintenb" : "=a" (old) : "aP" (mask) : "memory");
	return old;
}












 
struct thread_struct;
  void st20_resume(struct thread_struct *prev, struct thread_struct *next);

 
extern unsigned long st20_kstack;

 






static inline unsigned long __xchg(unsigned long x, volatile void *ptr, int size)
{
	unsigned long tmp = 0, flags;

	(( flags ) = __st20_gintdis(0)) ;
	((void) __st20_gintdis(7UL )) ;
	switch (size) {
	case 1:
		tmp = *(volatile unsigned char *) ptr;
		*(volatile unsigned char *) ptr = x;
		break;
	case 2:
		tmp = *(volatile unsigned short *) ptr;
		*(volatile unsigned short *) ptr = x;
		break;
	case 4:
		tmp = *(volatile unsigned long *) ptr;
		*(volatile unsigned long *) ptr = x;
		break;
	}
	do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
	return tmp;
}




extern void st20_reboot(void);



# 26 "../../include/linux/sched.h" 2

# 1 "../../include/asm/semaphore.h" 1






 



struct semaphore {
	int count;
	int waking;
	int lock;			 
	struct wait_queue * wait;
};




 
extern inline void get_buzz_lock(int *lock_ptr) { }
extern inline void give_buzz_lock(int *lock_ptr) { }

extern void __down(struct semaphore * sem);
extern int __down_interruptible(struct semaphore * sem);
extern void __up(struct semaphore * sem);

extern inline void down(struct semaphore * sem)
{
	unsigned long flags;
	int c;

	(( flags ) = __st20_gintdis(0)) ; ((void) __st20_gintdis(7UL )) ; c = --sem->count; do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
	if (c < 0)
		__down(sem);
}

extern inline int down_interruptible(struct semaphore * sem)
{
	unsigned long flags;
	int c;

	(( flags ) = __st20_gintdis(0)) ; ((void) __st20_gintdis(7UL )) ; c = --sem->count; do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
	return c < 0 ? __down_interruptible(sem) : 0;
}

extern inline void up(struct semaphore * sem)
{
	unsigned long flags;
	int c;

	(( flags ) = __st20_gintdis(0)) ; ((void) __st20_gintdis(7UL )) ; c = ++sem->count; do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
	if (c <= 0)
		__up(sem);
}


# 27 "../../include/linux/sched.h" 2

# 1 "../../include/asm/page.h" 1





 









 


typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd[16]; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;











# 53 "../../include/asm/page.h"


 


 






# 28 "../../include/linux/sched.h" 2


# 1 "../../include/linux/smp.h" 1



 




# 42 "../../include/linux/smp.h"


 


 







# 30 "../../include/linux/sched.h" 2

# 1 "../../include/linux/tty.h" 1



 



 






		 



# 1 "../../include/linux/fs.h" 1



 




# 1 "../../include/asm/byteorder.h" 1



 













extern unsigned long int	ntohl(unsigned long int);
extern unsigned short int	ntohs(unsigned short int);
extern unsigned long int	htonl(unsigned long int);
extern unsigned short int	htons(unsigned short int);












extern __inline__ unsigned long int __ntohl(unsigned long int x)
{
	return ((unsigned long int)((((unsigned long int)( x ) & 0x000000ffUL) << 24) | (((unsigned long int)( x ) & 0x0000ff00UL) <<  8) | (((unsigned long int)( x ) & 0x00ff0000UL) >>  8) | (((unsigned long int)( x ) & 0xff000000UL) >> 24))) ;
}

extern __inline__ unsigned short int __ntohs(unsigned short int x)
{
	return ((unsigned short int)((((unsigned short int)( x ) & 0x00ff) << 8) | (((unsigned short int)( x ) & 0xff00) >> 8))) ;
}










# 9 "../../include/linux/fs.h" 2



# 1 "../../include/linux/limits.h" 1






























# 12 "../../include/linux/fs.h" 2

# 1 "../../include/linux/wait.h" 1










struct wait_queue {
	struct task_struct * task;
	struct wait_queue * next;
};



static inline void init_waitqueue(struct wait_queue **q)
{
	*q = ((struct wait_queue *)(( q )-1)) ;
}

static inline int waitqueue_active(struct wait_queue **q)
{
	struct wait_queue *head = *q;
	return head && head != ((struct wait_queue *)(( q )-1)) ;
}

struct select_table_entry {
	struct wait_queue wait;
	struct wait_queue ** wait_address;
};

typedef struct select_table_struct {
	int nr;
	struct select_table_entry * entry;
} select_table;






# 13 "../../include/linux/fs.h" 2

# 1 "../../include/linux/types.h" 1



# 17 "../../include/linux/types.h"


# 1 "../../include/linux/posix_types.h" 1



 









 










 










  












typedef struct {
	unsigned long fds_bits [(1024 / (8 * sizeof(unsigned long)) ) ];
} __kernel_fd_set;

# 1 "../../include/asm/posix_types.h" 1



 





typedef unsigned short	__kernel_dev_t;
typedef unsigned long	__kernel_ino_t;
typedef unsigned short	__kernel_mode_t;
typedef unsigned short	__kernel_nlink_t;
typedef long		__kernel_off_t;
typedef int		__kernel_pid_t;
typedef unsigned short	__kernel_uid_t;
typedef unsigned short	__kernel_gid_t;
typedef unsigned long	__kernel_size_t;
typedef int		__kernel_ssize_t;
typedef int		__kernel_ptrdiff_t;
typedef long		__kernel_time_t;
typedef long		__kernel_clock_t;
typedef int		__kernel_daddr_t;
typedef char *		__kernel_caddr_t;


typedef long long	__kernel_loff_t;


typedef struct {

	int	val[2];



} __kernel_fsid_t;














# 53 "../../include/linux/posix_types.h" 2



# 19 "../../include/linux/types.h" 2

# 1 "../../include/asm/types.h" 1



 







typedef unsigned short umode_t;

 




typedef __signed__ char __s8;
typedef unsigned char __u8;

typedef __signed__ short __s16;
typedef unsigned short __u16;

typedef __signed__ int __s32;
typedef unsigned int __u32;


typedef __signed__ long long __s64;
typedef unsigned long long __u64;


 




typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

typedef signed long long s64;
typedef unsigned long long u64;




# 20 "../../include/linux/types.h" 2




typedef __kernel_fd_set		fd_set;
typedef __kernel_dev_t		dev_t;
typedef __kernel_ino_t		ino_t;
typedef __kernel_mode_t		mode_t;
typedef __kernel_nlink_t	nlink_t;
typedef __kernel_off_t		off_t;
typedef __kernel_pid_t		pid_t;
typedef __kernel_uid_t		uid_t;
typedef __kernel_gid_t		gid_t;
typedef __kernel_daddr_t	daddr_t;


typedef __kernel_loff_t		loff_t;


 





typedef __kernel_size_t		size_t;




typedef __kernel_ssize_t	ssize_t;




typedef __kernel_ptrdiff_t	ptrdiff_t;




typedef __kernel_time_t		time_t;




typedef __kernel_clock_t	clock_t;




typedef __kernel_caddr_t	caddr_t;


 
typedef unsigned char		u_char;
typedef unsigned short		u_short;
typedef unsigned int		u_int;
typedef unsigned long		u_long;

 
typedef unsigned char		unchar;
typedef unsigned short		ushort;
typedef unsigned int		uint;
typedef unsigned long		ulong;



 




struct ustat {
	__kernel_daddr_t	f_tfree;
	__kernel_ino_t		f_tinode;
	char			f_fname[6];
	char			f_fpack[6];
};


# 14 "../../include/linux/fs.h" 2

# 1 "../../include/linux/vfs.h" 1



# 1 "../../include/asm/statfs.h" 1







typedef __kernel_fsid_t	fsid_t;



struct statfs {
	long f_type;
	long f_bsize;
	long f_blocks;
	long f_bfree;
	long f_bavail;
	long f_files;
	long f_ffree;
	__kernel_fsid_t f_fsid;
	long f_namelen;
	long f_spare[6];
};


# 4 "../../include/linux/vfs.h" 2



# 15 "../../include/linux/fs.h" 2

# 1 "../../include/linux/net.h" 1
 





















# 1 "../../include/linux/socket.h" 1



# 1 "../../include/asm/socket.h" 1



# 1 "../../include/asm/sockios.h" 1



 








# 4 "../../include/asm/socket.h" 2


 
















 




# 4 "../../include/linux/socket.h" 2

# 1 "../../include/linux/sockios.h" 1
 





















 



 































 







 




 




 




 

 





 


 


 


# 5 "../../include/linux/socket.h" 2

# 1 "../../include/linux/uio.h" 1



 









 


struct iovec
{
	void *iov_base;		 
	int iov_len;
};





# 6 "../../include/linux/socket.h" 2


struct sockaddr 
{
	unsigned short	sa_family;	 
	char		sa_data[14];	 
};

struct linger {
	int		l_onoff;	 
	int		l_linger;	 
};

 




 
struct msghdr 
{
	void	*	msg_name;	 
	int		msg_namelen;	 
	struct iovec *	msg_iov;	 
	int 		msg_iovlen;	 
	void 	*	msg_control;	 
	int		msg_controllen;	 
	int		msg_flags;	 
};

 



 






					 
					 
					 
					 

 


















 

















 


 



 


 









 















 



 
 


 



 





extern void memcpy_fromiovec(unsigned char *kdata, struct iovec *iov, int len);
extern int verify_iovec(struct msghdr *m, struct iovec *iov, char *address, int mode);
extern void memcpy_toiovec(struct iovec *v, unsigned char *kdata, int len);
extern int move_addr_to_user(void *kaddr, int klen, void *uaddr, int *ulen);
extern int move_addr_to_kernel(void *uaddr, int ulen, void *kaddr);


# 23 "../../include/linux/net.h" 2
























typedef enum {
  SS_FREE = 0,				 
  SS_UNCONNECTED,			 
  SS_CONNECTING,			 
  SS_CONNECTED,				 
  SS_DISCONNECTING			 
} socket_state;






 










struct socket {
  short			type;		 
  socket_state		state;
  long			flags;
  struct proto_ops	*ops;		 
  void			*data;		 
  struct socket		*conn;		 
  struct socket		*iconn;		 
  struct socket		*next;
  struct wait_queue	**wait;		 
  struct inode		*inode;
  struct fasync_struct  *fasync_list;	 
  struct file		*file;		 
};



struct proto_ops {
  int	family;

  int	(*create)	(struct socket *sock, int protocol);
  int	(*dup)		(struct socket *newsock, struct socket *oldsock);
  int	(*release)	(struct socket *sock, struct socket *peer);
  int	(*bind)		(struct socket *sock, struct sockaddr *umyaddr,
			 int sockaddr_len);
  int	(*connect)	(struct socket *sock, struct sockaddr *uservaddr,
			 int sockaddr_len, int flags);
  int	(*socketpair)	(struct socket *sock1, struct socket *sock2);
  int	(*accept)	(struct socket *sock, struct socket *newsock,
			 int flags);
  int	(*getname)	(struct socket *sock, struct sockaddr *uaddr,
			 int *usockaddr_len, int peer);
  int	(*select)	(struct socket *sock, int sel_type,
			 select_table *wait);
  int	(*ioctl)	(struct socket *sock, unsigned int cmd,
			 unsigned long arg);
  int	(*listen)	(struct socket *sock, int len);
  int	(*shutdown)	(struct socket *sock, int flags);
  int	(*setsockopt)	(struct socket *sock, int level, int optname,
			 char *optval, int optlen);
  int	(*getsockopt)	(struct socket *sock, int level, int optname,
			 char *optval, int *optlen);
  int	(*fcntl)	(struct socket *sock, unsigned int cmd,
			 unsigned long arg);	
  int   (*sendmsg)	(struct socket *sock, struct msghdr *m, int total_len, int nonblock, int flags);
  int   (*recvmsg)	(struct socket *sock, struct msghdr *m, int total_len, int nonblock, int flags, int *addr_len);
};

struct net_proto {
	const char *name;		 
	void (*init_func)(struct net_proto *);	 
};

extern int	sock_wake_async(struct socket *sock, int how);
extern int	sock_register(int family, struct proto_ops *ops);
extern int	sock_unregister(int family);
extern struct socket *sock_alloc(void);
extern void	sock_release(struct socket *sock);


# 16 "../../include/linux/fs.h" 2

# 1 "../../include/linux/kdev_t.h" 1



 























































 






typedef unsigned short kdev_t;








extern char * kdevname(kdev_t);	 

 




static inline unsigned int kdev_t_to_nr(kdev_t dev) {
	return ((( dev ) >> 8 ) <<8) | (( dev ) & ((1<< 8 ) - 1) ) ;
}

static inline kdev_t to_kdev_t(int dev)
{
	int major, minor;








	major = (dev >> 8);
	minor = (dev & 0xff);

	return ((( major ) << 8 ) | (  minor )) ;
}

# 113 "../../include/linux/kdev_t.h"


# 17 "../../include/linux/fs.h" 2

# 1 "../../include/linux/ioctl.h" 1



# 1 "../../include/asm/ioctl.h" 1
 







 









 




 






















 












 





 





 








# 4 "../../include/linux/ioctl.h" 2




# 18 "../../include/linux/fs.h" 2


 








 



 






# 50 "../../include/linux/fs.h"




 
extern int max_inodes, nr_inodes;
extern int max_files, nr_files;






























 
















 




 





 


























 

















# 1 "../../include/asm/bitops.h" 1







 














extern __inline__ int set_bit(int nr, void * a)
{
	int 	* addr = a;
	int	mask, retval;
	unsigned long flags;

	addr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	(( flags ) = __st20_gintdis(0)) ; ((void) __st20_gintdis(7UL )) ;
	retval = (mask & *addr) != 0;
	*addr |= mask;
	do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
	return retval;
}

extern __inline__ int clear_bit(int nr, void * a)
{
	int 	* addr = a;
	int	mask, retval;
	unsigned long flags;

	addr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	(( flags ) = __st20_gintdis(0)) ; ((void) __st20_gintdis(7UL )) ;
	retval = (mask & *addr) != 0;
	*addr &= ~mask;
	do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
	return retval;
}

extern __inline__ unsigned long change_bit(unsigned long nr,  void *addr)
{
	int mask, flags;
	unsigned long *ADDR = (unsigned long *) addr;
	unsigned long oldbit;

	ADDR += nr >> 5;
	mask = 1 << (nr & 31);
	(( flags ) = __st20_gintdis(0)) ; ((void) __st20_gintdis(7UL )) ;
	oldbit = (mask & *ADDR);
	*ADDR ^= mask;
	do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
	return oldbit != 0;
}

extern __inline__ int test_bit(int nr, void * a)
{
	int 	* addr = a;
	int	mask;

	addr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	return ((mask & *addr) != 0);
}

 
extern __inline__ unsigned long ffz(unsigned long word)
{
	unsigned long result = 0;

	while(word & 1) {
		result++;
		word >>= 1;
	}
	return result;
}

 




extern __inline__ unsigned long find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = *(p++);
		tmp |= ~0UL >> (32-offset);
		if (size < 32)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size & ~31UL) {
		if (~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp |= ~0UL >> size;
found_middle:
	return result + ffz(tmp);
}

 






 

extern __inline__ int ext2_set_bit(int nr,void * addr)
{
	int		mask, retval, flags;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	(( flags ) = __st20_gintdis(0)) ; ((void) __st20_gintdis(7UL )) ;
	retval = (mask & *ADDR) != 0;
	*ADDR |= mask;
	do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
	return retval;
}

extern __inline__ int ext2_clear_bit(int nr, void * addr)
{
	int		mask, retval, flags;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	(( flags ) = __st20_gintdis(0)) ; ((void) __st20_gintdis(7UL )) ;
	retval = (mask & *ADDR) != 0;
	*ADDR &= ~mask;
	do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
	return retval;
}

extern __inline__ int ext2_test_bit(int nr, const void * addr)
{
	int			mask;
	const unsigned char	*ADDR = (const unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}




extern __inline__ unsigned long ext2_find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if(offset) {
		tmp = *(p++);
		tmp |= ~0UL << (32-offset);
		if(size < 32)
			goto found_first;
		if(~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while(size & ~31UL) {
		if(~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if(!size)
		return result;
	tmp = *p;

found_first:
	tmp |= ~0UL << size;
found_middle:
	tmp = ((tmp>>24) | ((tmp>>8)&0xff00) | ((tmp<<8)&0xff0000) | (tmp<<24));
	return result + ffz(tmp);
}




extern __inline__ int __ext2_test_bit(int nr, __const__ void * addr)
{
	int			mask;
	__const__ unsigned char	*ADDR = (__const__ unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}

extern __inline__ unsigned short __swab16(unsigned short value)
{
        return ((value >> 8) | (value << 8));
}     

extern __inline__ unsigned long __swab32(unsigned long value)
{
        return ((value >> 24) | ((value >> 8) & 0xff00) |
               ((value << 8) & 0xff0000) | (value << 24));
}     




extern __inline__ unsigned long __ext2_find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if(offset) {
		tmp = *(p++);
		tmp |= __swab32(~0UL >> (32-offset));
		if(size < 32)
			goto found_first;
		if(~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while(size & ~31UL) {
		if(~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if(!size)
		return result;
	tmp = *p;

found_first:
	return result + ffz(__swab32(tmp) | (~0UL << size));
found_middle:
	return result + ffz(__swab32(tmp));
}



# 160 "../../include/linux/fs.h" 2


extern void buffer_init(void);
extern unsigned long inode_init(unsigned long start, unsigned long end);
extern unsigned long file_table_init(unsigned long start, unsigned long end);
extern unsigned long name_cache_init(unsigned long start, unsigned long end);

typedef char buffer_block[1024 ];

 










 











struct buffer_head {
	 
	unsigned long b_blocknr;	 
	kdev_t b_dev;			 
	kdev_t b_rdev;			 
	unsigned long b_rsector;	 
	struct buffer_head * b_next;	 
	struct buffer_head * b_this_page;	 

	 
	unsigned long b_state;		 
	struct buffer_head * b_next_free;
	unsigned int b_count;		 
	unsigned long b_size;		 

	 
	char * b_data;			 
	unsigned int b_list;		 
	unsigned long b_flushtime;       

	unsigned long b_lru_time;        

	struct wait_queue * b_wait;
	struct buffer_head * b_prev;		 
	struct buffer_head * b_prev_free;	 
	struct buffer_head * b_reqnext;		 

 



	void * personality;
	void * private_bh;
};

static inline int buffer_uptodate(struct buffer_head * bh)
{
	return test_bit(0 , &bh->b_state);
}	

static inline int buffer_dirty(struct buffer_head * bh)
{
	return test_bit(1 , &bh->b_state);
}

static inline int buffer_locked(struct buffer_head * bh)
{
	return test_bit(2 , &bh->b_state);
}

static inline int buffer_req(struct buffer_head * bh)
{
	return test_bit(3 , &bh->b_state);
}

static inline int buffer_touched(struct buffer_head * bh)
{
	return test_bit(4 , &bh->b_state);
}

static inline int buffer_has_aged(struct buffer_head * bh)
{
	return test_bit(5 , &bh->b_state);
}

static inline int buffer_protected(struct buffer_head * bh)
{
	return test_bit(6 , &bh->b_state);
}

# 1 "../../include/linux/pipe_fs_i.h" 1



struct pipe_inode_info {
	struct wait_queue * wait;
	char * base;
	unsigned int start;
	unsigned int len;
	unsigned int lock;
	unsigned int rd_openers;
	unsigned int wr_openers;
	unsigned int readers;
	unsigned int writers;
};





















# 262 "../../include/linux/fs.h" 2

# 1 "../../include/linux/minix_fs_i.h" 1



 


struct minix_inode_info {
	union {
		__u16 i1_data[16];
		__u32 i2_data[16];
	} u;
};


# 263 "../../include/linux/fs.h" 2

# 1 "../../include/linux/ext_fs_i.h" 1



 


struct ext_inode_info {
	unsigned long i_data[16];
};


# 264 "../../include/linux/fs.h" 2

# 1 "../../include/linux/ext2_fs_i.h" 1
 

















 


struct ext2_inode_info {
	__u32	i_data[15];
	__u32	i_flags;
	__u32	i_faddr;
	__u8	i_frag_no;
	__u8	i_frag_size;
	__u16	i_osync;
	__u32	i_file_acl;
	__u32	i_dir_acl;
	__u32	i_dtime;
	__u32	i_version;
	__u32	i_block_group;
	__u32	i_next_alloc_block;
	__u32	i_next_alloc_goal;
	__u32	i_prealloc_block;
	__u32	i_prealloc_count;
	int	i_new_inode:1;	 
};


# 265 "../../include/linux/fs.h" 2

# 1 "../../include/linux/hpfs_fs_i.h" 1



struct hpfs_inode_info {
	ino_t i_parent_dir;	 
	unsigned i_dno;		 
	unsigned i_dpos;	 
	unsigned i_dsubdno;	 
	unsigned i_file_sec;	 
	unsigned i_disk_sec;	 
	unsigned i_n_secs;	 
	unsigned i_conv : 2;	 
};











# 266 "../../include/linux/fs.h" 2

# 1 "../../include/linux/msdos_fs_i.h" 1







 



struct msdos_inode_info {
	 













	struct pipe_inode_info reserved;
	int i_start;	 
	int i_logstart;	 
	int i_attrs;	 
	int i_busy;	 

	struct inode *i_depend;  

	struct inode *i_old;	 

	struct inode *i_linked;	 

	struct inode *i_oldlink; 

	int i_binary;	 
};


# 267 "../../include/linux/fs.h" 2

# 1 "../../include/linux/umsdos_fs_i.h" 1









 




































struct dir_locking_info {
	struct wait_queue *p;
	short int looking;		 
	short int creating;		 
							 
							 
	long pid;				 
							 
};
struct umsdos_inode_info {
	union {
		struct msdos_inode_info msdos_info;
		struct pipe_inode_info pipe_info;
		struct dir_locking_info dir_info;
	}u;	 
	unsigned long i_dir_owner;	 
								 
	unsigned long i_emd_owner;	 
	off_t pos;					 
	 
	unsigned long i_emd_dir;	 
};


# 268 "../../include/linux/fs.h" 2

# 1 "../../include/linux/iso_fs_i.h" 1



 


struct iso_inode_info {
	unsigned int i_first_extent;
	unsigned int i_backlink;
	unsigned char i_file_format;
	unsigned long i_next_section_ino;
	off_t i_section_size;
};


# 269 "../../include/linux/fs.h" 2

# 1 "../../include/linux/nfs_fs_i.h" 1



# 1 "../../include/linux/nfs.h" 1
























enum rpc_auth_flavor {
	RPC_AUTH_NULL = 0,
	RPC_AUTH_UNIX = 1,
	RPC_AUTH_SHORT = 2
};

enum rpc_msg_type {
	RPC_CALL = 0,
	RPC_REPLY = 1
};

enum rpc_reply_stat {
	RPC_MSG_ACCEPTED = 0,
	RPC_MSG_DENIED = 1
};

enum rpc_accept_stat {
	RPC_SUCCESS = 0,
	RPC_PROG_UNAVAIL = 1,
	RPC_PROG_MISMATCH = 2,
	RPC_PROC_UNAVAIL = 3,
	RPC_GARBAGE_ARGS = 4
};

enum rpc_reject_stat {
	RPC_MISMATCH = 0,
	RPC_AUTH_ERROR = 1
};

enum rpc_auth_stat {
	RPC_AUTH_BADCRED = 1,
	RPC_AUTH_REJECTEDCRED = 2,
	RPC_AUTH_BADVERF = 3,
	RPC_AUTH_REJECTEDVERF = 4,
	RPC_AUTH_TOOWEAK = 5
};


	
enum nfs_stat {
	NFS_OK = 0,
	NFSERR_PERM = 1,
	NFSERR_NOENT = 2,
	NFSERR_IO = 5,
	NFSERR_NXIO = 6,
	NFSERR_EAGAIN = 11,
	NFSERR_ACCES = 13,
	NFSERR_EXIST = 17,
	NFSERR_XDEV = 18,
	NFSERR_NODEV = 19,
	NFSERR_NOTDIR = 20,
	NFSERR_ISDIR = 21,
	NFSERR_INVAL = 22,	 
	NFSERR_FBIG = 27,
	NFSERR_NOSPC = 28,
	NFSERR_ROFS = 30,
	NFSERR_NAMETOOLONG = 63,
	NFSERR_NOTEMPTY = 66,
	NFSERR_DQUOT = 69,
	NFSERR_STALE = 70,
	NFSERR_WFLUSH = 99
};

enum nfs_ftype {
	NFNON = 0,
	NFREG = 1,
	NFDIR = 2,
	NFBLK = 3,
	NFCHR = 4,
	NFLNK = 5,
	NFSOCK = 6,
	NFBAD = 7,
	NFFIFO = 8
};






















struct nfs_fh {
	char data[32 ];
};

struct nfs_time {
	u_int seconds;
	u_int useconds;
};

struct nfs_fattr {
	enum nfs_ftype type;
	u_int mode;
	u_int nlink;
	u_int uid;
	u_int gid;
	u_int size;
	u_int blocksize;
	u_int rdev;
	u_int blocks;
	u_int fsid;
	u_int fileid;
	struct nfs_time atime;
	struct nfs_time mtime;
	struct nfs_time ctime;
};

struct nfs_sattr {
	u_int mode;
	u_int uid;
	u_int gid;
	u_int size;
	struct nfs_time atime;
	struct nfs_time mtime;
};

struct nfs_entry {
	u_int fileid;
	char *name;
	int cookie;
	int eof;
};

struct nfs_fsinfo {
	u_int tsize;
	u_int bsize;
	u_int blocks;
	u_int bfree;
	u_int bavail;
};


# 4 "../../include/linux/nfs_fs_i.h" 2



 


struct nfs_inode_info {
	struct pipe_inode_info pipeinfo;
	struct nfs_fh fhandle;
	 









	unsigned long read_cache_jiffies;
	unsigned long read_cache_mtime;
	 




	struct inode *silly_rename_dir;
	 


	unsigned long attrtimeo;
};


# 270 "../../include/linux/fs.h" 2

# 1 "../../include/linux/xia_fs_i.h" 1



 








struct xiafs_inode_info { 		 
    unsigned long  i_zone[8];
    unsigned long  i_ind_zone;
    unsigned long  i_dind_zone;
};


# 271 "../../include/linux/fs.h" 2

# 1 "../../include/linux/sysv_fs_i.h" 1



 


struct sysv_inode_info {
	unsigned long i_data[10+1+1+1];	 




};



# 272 "../../include/linux/fs.h" 2

# 1 "../../include/linux/affs_fs_i.h" 1






 


struct affs_inode_info {
	__u32	i_protect;		 
	__s32	i_parent;		 
	__s32	i_original;		 
	__s32	i_ext[12 ];	 
	__s32	i_data[8 ];	 
	int	i_lastblock;		 
	short	i_max_ext;		 
	short	i_pa_cnt;		 
	short	i_pa_next;		 
	short	i_pa_last;		 
	short	i_zone;			 
	unsigned char i_hlink;		 
	unsigned char i_pad;
};


# 273 "../../include/linux/fs.h" 2

# 1 "../../include/linux/ufs_fs_i.h" 1
 














# 1 "../../include/linux/ufs_fs.h" 1
 
















# 1 "../../include/linux/time.h" 1





struct timespec {
	long	tv_sec;		 
	long	tv_nsec;	 
};


struct timeval {
	long	tv_sec;		 
	long	tv_usec;	 
};

struct timezone {
	int	tz_minuteswest;	 
	int	tz_dsttime;	 
};




void do_gettimeofday(struct timeval *tv);
void do_settimeofday(struct timeval *tv);








 







struct  itimerspec {
        struct  timespec it_interval;     
        struct  timespec it_value;        
};

struct	itimerval {
	struct	timeval it_interval;	 
	struct	timeval it_value;	 
};


# 18 "../../include/linux/ufs_fs.h" 2

# 1 "../../include/linux/stat.h" 1





# 1 "../../include/asm/stat.h" 1



struct old_stat {
	unsigned short st_dev;
	unsigned short st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned long  st_size;
	unsigned long  st_atime;
	unsigned long  st_mtime;
	unsigned long  st_ctime;
};

struct new_stat {
	unsigned short st_dev;
	unsigned short __pad1;
	unsigned long st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned short __pad2;
	unsigned long  st_size;
	unsigned long  st_blksize;
	unsigned long  st_blocks;
	unsigned long  st_atime;
	unsigned long  __unused1;
	unsigned long  st_mtime;
	unsigned long  __unused2;
	unsigned long  st_ctime;
	unsigned long  __unused3;
	unsigned long  __unused4;
	unsigned long  __unused5;
};


# 6 "../../include/linux/stat.h" 2
















































# 19 "../../include/linux/ufs_fs.h" 2

























 






 



 



 



 
 




struct ufs_direct {
	__u32  d_ino;			 
	__u16  d_reclen;		 
	__u16  d_namlen;		 
	char   d_name[255  + 1];	 
};




struct ufs_csum {
	__u32	cs_ndir;	 
	__u32	cs_nbfree;	 
	__u32	cs_nifree;	 
	__u32	cs_nffree;	 
};

typedef struct _ufsquad {
	__u32 val[2];
} ufsquad;

 


struct ufs_superblock {
	__u32	fs_link;	 
	__u32	fs_rlink;	 
	__u32	fs_sblkno;
	__u32	fs_cblkno;
	__u32	fs_iblkno;
	__u32	fs_dblkno;
	__u32	fs_cgoffset;
	__u32	fs_cgmask;
	time_t	fs_time;	 
	__u32	fs_size;
	__u32	fs_dsize;
	__u32	fs_ncg;
	__u32	fs_bsize;
	__u32	fs_fsize;
	__u32	fs_frag;
	__u32	fs_minfree;
	__u32	fs_rotdelay;
	__u32	fs_rps;
	__u32	fs_bmask;
	__u32	fs_fmask;
	__u32	fs_bshift;
	__u32	fs_fshift;
	__u32	fs_maxcontig;
	__u32	fs_maxbpg;
	__u32	fs_fragshift;
	__u32	fs_fsbtodb;
	__u32	fs_sbsize;
	__u32	fs_csmask;
	__u32	fs_csshift;
	__u32	fs_nindir;
	__u32	fs_inopb;
	__u32	fs_nspf;
	__u32	fs_optim;
	__u32	fs_XXX1;
	__u32	fs_interleave;
	__u32	fs_trackskew;
	__u32	fs_id[2];
	__u32	fs_csaddr;
	__u32	fs_cssize;
	__u32	fs_cgsize;
	__u32	fs_ntrak;
	__u32	fs_nsect;
	__u32	fs_spc;
	__u32	fs_ncyl;
	__u32	fs_cpg;
	__u32	fs_ipg;
	__u32	fs_fpg;
	struct ufs_csum fs_cstotal;
	__u8	fs_fmod;
	__u8	fs_clean;
	__u8	fs_ronly;
	__u8	fs_flags;
	__u8	fs_fsmnt[512 ];
	__u32	fs_cgrotor;
	struct ufs_csum * fs_csp[32 ];
	__u32	fs_cpc;
	__u16	fs_opostbl[16][8];	 
	__s32	fs_sparecon[55];	 
	__s32	fs_state;		 
	ufsquad	fs_qbmask;		 
	ufsquad	fs_qfmask;		 
	__s32	fs_postblformat;	 
	__s32	fs_nrpos;		 
	__s32	fs_postbloff;		 
	__s32	fs_rotbloff;		 
	__s32	fs_magic;		 
	__u8	fs_space[1];		 

};

 


struct ufs_inode {
	__u16	ui_mode;		 
	__u16	ui_nlink;		 
	__u16	ui_suid;		 
	__u16	ui_sgid;		 
	ufsquad	ui_size;		    
	struct timeval ui_atime;	 
	struct timeval ui_mtime;	 
	struct timeval ui_ctime;	 
	__u32	ui_db[12 ];		 
	__u32	ui_ib[3 ];		 
	__u32	ui_flags;		 
	__u32	ui_blocks;		 
	__u32	ui_gen;			 
	__u32	ui_shadow;		 
	__u32	ui_uid;			 
	__u32	ui_gid;			 
	__u32	ui_oeftflag;		 
};

extern int init_ufs_fs(void);


 








# 16 "../../include/linux/ufs_fs_i.h" 2


struct ufs_inode_info {
	__u64	ui_size;
	__u32	ui_flags;
	__u32	ui_gen;
	__u32	ui_shadow;
	__u32	ui_uid;
	__u32	ui_gid;
	__u32	ui_oeftflag;
	__u32	ui_db[12 ];
	__u32	ui_ib[3 ];
};


# 274 "../../include/linux/fs.h" 2

# 1 "../../include/linux/romfs_fs_i.h" 1



 

struct romfs_inode_info {
	unsigned long i_metasize;	 
	unsigned long i_dataoffset;	 
};


# 275 "../../include/linux/fs.h" 2


 














 








struct iattr {
	unsigned int	ia_valid;
	umode_t		ia_mode;
	uid_t		ia_uid;
	gid_t		ia_gid;
	off_t		ia_size;
	time_t		ia_atime;
	time_t		ia_mtime;
	time_t		ia_ctime;
};

# 1 "../../include/linux/quota.h" 1
 








































# 1 "../../include/linux/errno.h" 1



# 1 "../../include/asm/errno.h" 1




































































































































# 4 "../../include/linux/errno.h" 2




 








# 42 "../../include/linux/quota.h" 2


 






 





 















 














 


















 




struct dqblk {
    __u32 dqb_bhardlimit;	 
    __u32 dqb_bsoftlimit;	 
    __u32 dqb_curblocks;	 
    __u32 dqb_ihardlimit;	 
    __u32 dqb_isoftlimit;	 
    __u32 dqb_curinodes;	 
    time_t dqb_btime;		 
    time_t dqb_itime;		 
};

 













struct dqstats {
   __u32 lookups;
   __u32 drops;
   __u32 reads;
   __u32 writes;
   __u32 cache_hits;
   __u32 pages_allocated;
   __u32 allocated_dquots;
   __u32 free_dquots;
   __u32 syncs;
};



# 1 "../../include/linux/mount.h" 1
 












struct vfsmount
{
   kdev_t mnt_dev;                      
   char *mnt_devname;                   
   char *mnt_dirname;                   
   unsigned int mnt_flags;              
   struct semaphore mnt_sem;            
   struct super_block *mnt_sb;          
   struct file *mnt_quotas[2 ];  
   time_t mnt_iexp[2 ];          
   time_t mnt_bexp[2 ];          
   struct vfsmount *mnt_next;           
};

struct vfsmount *lookup_vfsmnt(kdev_t dev);


# 151 "../../include/linux/quota.h" 2


 












struct dquot {
   unsigned int dq_id;		 
   short dq_type;		 
   kdev_t dq_dev;                 
   short dq_flags;		 
   short dq_count;		 
   short dq_locknest;   	 	
   struct task_struct *dq_lockproc;	 
   struct vfsmount *dq_mnt;      
   struct dqblk dq_dqb;          
   struct wait_queue *dq_wait;	 
   struct dquot *dq_prev;	 
   struct dquot *dq_next;	 
   struct dquot *dq_hash_prev;	 
   struct dquot *dq_hash_next;	 
};



 










 



extern void dquot_initialize(struct inode *inode, short type);
extern void dquot_drop(struct inode *inode);
extern int dquot_alloc_block(const struct inode *inode, unsigned long number);
extern int dquot_alloc_inode(const struct inode *inode, unsigned long number);
extern void dquot_free_block(const struct inode *inode, unsigned long number);
extern void dquot_free_inode(const struct inode *inode, unsigned long number);
extern int dquot_transfer(struct inode *inode, struct iattr *iattr, char direction);

extern void invalidate_dquots(kdev_t dev, short type);
extern int quota_off(kdev_t dev, short type);
extern int sync_dquots(kdev_t dev, short type);

# 220 "../../include/linux/quota.h"


# 312 "../../include/linux/fs.h" 2


struct vm_area_struct;

struct inode {
	kdev_t		i_dev;
	unsigned long	i_ino;
	umode_t		i_mode;
	nlink_t		i_nlink;
	uid_t		i_uid;
	gid_t		i_gid;
	kdev_t		i_rdev;
	off_t		i_size;
	time_t		i_atime;
	time_t		i_mtime;
	time_t		i_ctime;
	unsigned long	i_blksize;
	unsigned long	i_blocks;
	unsigned long	i_version;
	unsigned long	i_nrpages;
	struct semaphore i_sem;
	struct inode_operations *i_op;
	struct super_block *i_sb;
	struct wait_queue *i_wait;
	struct file_lock *i_flock;



	struct page *i_pages;
	struct dquot *i_dquot[2 ];
	struct inode *i_next, *i_prev;
	struct inode *i_hash_next, *i_hash_prev;
	struct inode *i_bound_to, *i_bound_by;
	struct inode *i_mount;
	unsigned long i_count;	 
	unsigned short i_flags;
	unsigned short i_writecount;
	unsigned char i_lock;
	unsigned char i_dirt;
	unsigned char i_pipe;
	unsigned char i_sock;
	unsigned char i_seek;
	unsigned char i_update;
 	unsigned char i_condemned;
	union {
		struct pipe_inode_info pipe_i;
		struct minix_inode_info minix_i;
		struct ext_inode_info ext_i;
		struct ext2_inode_info ext2_i;
		struct hpfs_inode_info hpfs_i;
		struct msdos_inode_info msdos_i;
		struct umsdos_inode_info umsdos_i;
		struct iso_inode_info isofs_i;
		struct nfs_inode_info nfs_i;
		struct xiafs_inode_info xiafs_i;
		struct sysv_inode_info sysv_i;
		struct affs_inode_info affs_i;
		struct ufs_inode_info ufs_i;
		struct romfs_inode_info romfs_i;
		struct socket socket_i;
		void * generic_ip;
	} u;
};

struct fown_struct {
	int pid;		 
	uid_t uid, euid;	 
};

struct file {
	mode_t f_mode;
	loff_t f_pos;
	unsigned short f_flags;
	unsigned short f_count;
	unsigned long f_reada, f_ramax, f_raend, f_ralen, f_rawin;
	struct file *f_next, *f_prev;
	struct fown_struct f_owner;
	struct inode * f_inode;
	struct file_operations * f_op;
	unsigned long f_version;
	void *private_data;	 
};






struct file_lock {
	struct file_lock *fl_next;	 
	struct file_lock *fl_nextlink;	 
	struct file_lock *fl_prevlink;	 
	struct file_lock *fl_nextblock;  
	struct file_lock *fl_prevblock;
	struct task_struct *fl_owner;
	struct wait_queue *fl_wait;
	struct file *fl_file;
	unsigned char fl_flags;
	unsigned char fl_type;
	off_t fl_start;
	off_t fl_end;
};

# 1 "../../include/linux/fcntl.h" 1



# 1 "../../include/asm/fcntl.h" 1



 



























 


 




 



 






struct flock {
	short l_type;
	short l_whence;
	off_t l_start;
	off_t l_len;
	pid_t l_pid;
};


# 4 "../../include/linux/fcntl.h" 2



# 415 "../../include/linux/fs.h" 2


extern int fcntl_getlk(unsigned int fd, struct flock *l);
extern int fcntl_setlk(unsigned int fd, unsigned int cmd, struct flock *l);
extern void locks_remove_locks(struct task_struct *task, struct file *filp);






extern int locks_mandatory_locked(struct inode *inode);
extern int locks_mandatory_area(int read_write, struct inode *inode,
				struct file *filp, unsigned int offset,
				unsigned int count);

extern inline int locks_verify_locked(struct inode *inode)
{
	 


	if ((( inode )->i_flags & 64 )  &&
	    (inode->i_mode & (0002000  | 00010 )) == 0002000 )
		return (locks_mandatory_locked(inode));
	return (0);
}
extern inline int locks_verify_area(int read_write, struct inode *inode,
				    struct file *filp, unsigned int offset,
				    unsigned int count)
{
	 


	if ((( inode )->i_flags & 64 )  &&
	    (inode->i_mode & (0002000  | 00010 )) == 0002000 )
		return (locks_mandatory_area(read_write, inode, filp, offset,
					     count));
	return (0);
}

struct fasync_struct {
	int    magic;
	struct fasync_struct	*fa_next;  
	struct file 		*fa_file;
};



extern int fasync_helper(struct inode *, struct file *, int, struct fasync_struct **);

# 1 "../../include/linux/minix_fs_sb.h" 1



 


struct minix_sb_info {
			unsigned long s_ninodes;
			unsigned long s_nzones;
			unsigned long s_imap_blocks;
			unsigned long s_zmap_blocks;
			unsigned long s_firstdatazone;
			unsigned long s_log_zone_size;
			unsigned long s_max_size;
			struct buffer_head * s_imap[8];
			struct buffer_head * s_zmap[64];
			unsigned long s_dirsize;
			unsigned long s_namelen;
			struct buffer_head * s_sbh;
			struct minix_super_block * s_ms;
			unsigned short s_mount_state;
			unsigned short s_version;
};


# 465 "../../include/linux/fs.h" 2

# 1 "../../include/linux/ext_fs_sb.h" 1



 


struct ext_sb_info {
			unsigned long s_ninodes;
			unsigned long s_nzones;
			unsigned long s_firstdatazone;
			unsigned long s_log_zone_size;
			unsigned long s_max_size;
			unsigned long s_firstfreeblocknumber;
			unsigned long s_freeblockscount;
			struct buffer_head * s_firstfreeblock;
			unsigned long s_firstfreeinodenumber;
			unsigned long s_freeinodescount;
			struct buffer_head * s_firstfreeinodeblock;
};


# 466 "../../include/linux/fs.h" 2

# 1 "../../include/linux/ext2_fs_sb.h" 1
 

















# 1 "../../include/linux/ext2_fs.h" 1
 



















 



 




 




 





 












 









 


 




 




 






























 













 


struct ext2_acl_header	 
{
	__u32	aclh_size;
	__u32	aclh_file_count;
	__u32	aclh_acle_count;
	__u32	aclh_first_acle;
};

struct ext2_acl_entry	 
{
	__u32	acle_size;
	__u16	acle_perms;	 
	__u16	acle_type;	 
	__u16	acle_tag;	 
	__u16	acle_pad1;
	__u32	acle_next;	 
					 
};

 


struct ext2_group_desc
{
	__u32	bg_block_bitmap;		 
	__u32	bg_inode_bitmap;		 
	__u32	bg_inode_table;		 
	__u16	bg_free_blocks_count;	 
	__u16	bg_free_inodes_count;	 
	__u16	bg_used_dirs_count;	 
	__u16	bg_pad;
	__u32	bg_reserved[3];
};

 













 








 











	
 







 


struct ext2_inode {
	__u16	i_mode;		 
	__u16	i_uid;		 
	__u32	i_size;		 
	__u32	i_atime;	 
	__u32	i_ctime;	 
	__u32	i_mtime;	 
	__u32	i_dtime;	 
	__u16	i_gid;		 
	__u16	i_links_count;	 
	__u32	i_blocks;	 
	__u32	i_flags;	 
	union {
		struct {
			__u32  l_i_reserved1;
		} linux1;
		struct {
			__u32  h_i_translator;
		} hurd1;
		struct {
			__u32  m_i_reserved1;
		} masix1;
	} osd1;				 
	__u32	i_block[(((12   + 1)  + 1)  + 1) ]; 
	__u32	i_version;	 
	__u32	i_file_acl;	 
	__u32	i_dir_acl;	 
	__u32	i_faddr;	 
	union {
		struct {
			__u8	l_i_frag;	 
			__u8	l_i_fsize;	 
			__u16	i_pad1;
			__u32	l_i_reserved2[2];
		} linux2;
		struct {
			__u8	h_i_frag;	 
			__u8	h_i_fsize;	 
			__u16	h_i_mode_high;
			__u16	h_i_uid_high;
			__u16	h_i_gid_high;
			__u32	h_i_author;
		} hurd2;
		struct {
			__u8	m_i_frag;	 
			__u8	m_i_fsize;	 
			__u16	m_pad1;
			__u32	m_i_reserved2[2];
		} masix2;
	} osd2;				 
};
























 





 


















 





 







 


struct ext2_super_block {
	__u32	s_inodes_count;		 
	__u32	s_blocks_count;		 
	__u32	s_r_blocks_count;	 
	__u32	s_free_blocks_count;	 
	__u32	s_free_inodes_count;	 
	__u32	s_first_data_block;	 
	__u32	s_log_block_size;	 
	__s32	s_log_frag_size;	 
	__u32	s_blocks_per_group;	 
	__u32	s_frags_per_group;	 
	__u32	s_inodes_per_group;	 
	__u32	s_mtime;		 
	__u32	s_wtime;		 
	__u16	s_mnt_count;		 
	__s16	s_max_mnt_count;	 
	__u16	s_magic;		 
	__u16	s_state;		 
	__u16	s_errors;		 
	__u16	s_minor_rev_level; 	 
	__u32	s_lastcheck;		 
	__u32	s_checkinterval;	 
	__u32	s_creator_os;		 
	__u32	s_rev_level;		 
	__u16	s_def_resuid;		 
	__u16	s_def_resgid;		 
	 












	__u32	s_first_ino; 		 
	__u16   s_inode_size; 		 
	__u16	s_block_group_nr; 	 
	__u32	s_feature_compat; 	 
	__u32	s_feature_incompat; 	 
	__u32	s_feature_ro_compat; 	 
	__u32	s_reserved[230];	 
};

 








 










 





 




struct ext2_dir_entry {
	__u32	inode;			 
	__u16	rec_len;		 
	__u16	name_len;		 
	char	name[255 ];	 
};

 









 







 



 







 
extern int ext2_permission (struct inode *, int);

 
extern int ext2_new_block (const struct inode *, unsigned long,
			   __u32 *, __u32 *, int *);
extern void ext2_free_blocks (const struct inode *, unsigned long,
			      unsigned long);
extern unsigned long ext2_count_free_blocks (struct super_block *);
extern void ext2_check_blocks_bitmap (struct super_block *);

 
extern unsigned long ext2_count_free (struct buffer_head *, unsigned);

 
extern int ext2_check_dir_entry (const char *, struct inode *,
				 struct ext2_dir_entry *, struct buffer_head *,
				 unsigned long);

 
extern int ext2_read (struct inode *, struct file *, char *, int);
extern int ext2_write (struct inode *, struct file *, char *, int);

 
extern int ext2_sync_file (struct inode *, struct file *);

 
extern struct inode * ext2_new_inode (const struct inode *, int, int *);
extern void ext2_free_inode (struct inode *);
extern unsigned long ext2_count_free_inodes (struct super_block *);
extern void ext2_check_inodes_bitmap (struct super_block *);

 
extern int ext2_bmap (struct inode *, int);

extern struct buffer_head * ext2_getblk (struct inode *, long, int, int *);
extern struct buffer_head * ext2_bread (struct inode *, int, int, int *);

extern int ext2_getcluster (struct inode * inode, long block);
extern void ext2_read_inode (struct inode *);
extern void ext2_write_inode (struct inode *);
extern void ext2_put_inode (struct inode *);
extern int ext2_sync_inode (struct inode *);
extern void ext2_discard_prealloc (struct inode *);

 
extern int ext2_ioctl (struct inode *, struct file *, unsigned int,
		       unsigned long);

 
extern void ext2_release (struct inode *, struct file *);
extern int ext2_lookup (struct inode *,const char *, int, struct inode **);
extern int ext2_create (struct inode *,const char *, int, int,
			struct inode **);
extern int ext2_mkdir (struct inode *, const char *, int, int);
extern int ext2_rmdir (struct inode *, const char *, int);
extern int ext2_unlink (struct inode *, const char *, int);
extern int ext2_symlink (struct inode *, const char *, int, const char *);
extern int ext2_link (struct inode *, struct inode *, const char *, int);
extern int ext2_mknod (struct inode *, const char *, int, int, int);
extern int ext2_rename (struct inode *, const char *, int,
			struct inode *, const char *, int, int);

 
extern void ext2_error (struct super_block *, const char *, const char *, ...)
	__attribute__ ((format (printf, 3, 4)));
extern   void ext2_panic (struct super_block *, const char *,
				   const char *, ...)
	__attribute__ ((noreturn,  format (printf, 3, 4)));
extern void ext2_warning (struct super_block *, const char *, const char *, ...)
	__attribute__ ((format (printf, 3, 4)));
extern void ext2_put_super (struct super_block *);
extern void ext2_write_super (struct super_block *);
extern int ext2_remount (struct super_block *, int *, char *);
extern struct super_block * ext2_read_super (struct super_block *,void *,int);
extern int init_ext2_fs(void);
extern void ext2_statfs (struct super_block *, struct statfs *, int);

 
extern void ext2_truncate (struct inode *);

 



 
extern struct inode_operations ext2_dir_inode_operations;

 
extern struct inode_operations ext2_file_inode_operations;

 
extern struct inode_operations ext2_symlink_inode_operations;




# 19 "../../include/linux/ext2_fs_sb.h" 2


 



 



 


struct ext2_sb_info {
	unsigned long s_frag_size;	 
	unsigned long s_frags_per_block; 
	unsigned long s_inodes_per_block; 
	unsigned long s_frags_per_group; 
	unsigned long s_blocks_per_group; 
	unsigned long s_inodes_per_group; 
	unsigned long s_itb_per_group;	 
	unsigned long s_db_per_group;	 
	unsigned long s_desc_per_block;	 
	unsigned long s_groups_count;	 
	struct buffer_head * s_sbh;	 
	struct ext2_super_block * s_es;	 
	struct buffer_head ** s_group_desc;
	unsigned short s_loaded_inode_bitmaps;
	unsigned short s_loaded_block_bitmaps;
	unsigned long s_inode_bitmap_number[8 ];
	struct buffer_head * s_inode_bitmap[8 ];
	unsigned long s_block_bitmap_number[8 ];
	struct buffer_head * s_block_bitmap[8 ];
	int s_rename_lock;
	struct wait_queue * s_rename_wait;
	unsigned long  s_mount_opt;
	unsigned short s_resuid;
	unsigned short s_resgid;
	unsigned short s_mount_state;
	unsigned short s_pad;
	int s_addr_per_block_bits;
	int s_desc_per_block_bits;
	int s_inode_size;
	int s_first_ino;
};


# 467 "../../include/linux/fs.h" 2

# 1 "../../include/linux/hpfs_fs_sb.h" 1



struct hpfs_sb_info {
	ino_t sb_root;			 
	unsigned sb_fs_size;		 
	unsigned sb_bitmaps;		 
	unsigned sb_dirband_size;	 
	unsigned sb_dmap;		 
	unsigned sb_n_free;		 
	unsigned sb_n_free_dnodes;	 
	uid_t sb_uid;			 
	gid_t sb_gid;			 
	umode_t sb_mode;		 
	unsigned sb_lowercase : 1;	 
	unsigned sb_conv : 2;		 
};















# 468 "../../include/linux/fs.h" 2

# 1 "../../include/linux/msdos_fs_sb.h" 1



 



struct fat_mount_options {
	uid_t fs_uid;
	gid_t fs_gid;
	unsigned short fs_umask;
	unsigned short codepage;   
	char *iocharset;           
	unsigned char name_check;  
	unsigned char conversion;  
	unsigned quiet:1,          
		 showexec:1,       
		 sys_immutable:1,  
		 dotsOK:1,         
		 isvfat:1,         
		 utf8:1,	   
		 unicode_xlate:1,  
		 posixfs:1,        
		 numtail:1,        
		 fat32:1;	   
};

struct vfat_unicode {
	unsigned char uni1;
	unsigned char uni2;
};

struct msdos_sb_info {
	unsigned short cluster_size;  
	unsigned char fats,fat_bits;  
	unsigned short fat_start,fat_length;  
	unsigned short dir_start,dir_entries;  
	unsigned short data_start;    
	unsigned long clusters;       
	unsigned long root_cluster;   
	unsigned long fsinfo_offset;  
	struct wait_queue *fat_wait;
	int fat_lock;
	int prev_free;                
	int free_clusters;            
	struct fat_mount_options options;
	struct nls_table *nls_disk;   
	struct nls_table *nls_io;     
};


# 469 "../../include/linux/fs.h" 2

# 1 "../../include/linux/iso_fs_sb.h" 1



 


struct isofs_sb_info {
	unsigned long s_ninodes;
	unsigned long s_nzones;
	unsigned long s_firstdatazone;
	unsigned long s_log_zone_size;
	unsigned long s_max_size;
	
	unsigned char s_high_sierra;  
	unsigned char s_mapping;
	unsigned char s_conversion;
	unsigned char s_rock;
	unsigned char s_joliet_level;
	unsigned char s_utf8;
	unsigned char s_name_check;  
	unsigned char s_cruft;  


	unsigned char s_unhide;
	unsigned char s_nosuid;
	unsigned char s_nodev;
	mode_t s_mode;
	gid_t s_gid;
	uid_t s_uid;
	struct nls_table *s_nls_iocharset;  
};









# 470 "../../include/linux/fs.h" 2

# 1 "../../include/linux/nfs_fs_sb.h" 1



# 1 "../../include/linux/rpcsock.h" 1
 









 

















 



 




 


struct rpc_timeout {
	unsigned long		to_initval,
				to_maxval,
				to_increment;
	int			to_retries;
	char			to_exponential;
};

 


struct rpc_ioreq {
	struct rpc_wait *	rq_slot;
	struct sockaddr	*	rq_addr;
	int			rq_alen;
	struct iovec		rq_svec[16 ];
	unsigned int		rq_snr;
	unsigned long		rq_slen;
	struct iovec		rq_rvec[16 ];
	unsigned int		rq_rnr;
	unsigned long		rq_rlen;
};

 


struct rpc_wait;
typedef void	(*rpc_callback_fn_t)(int, struct rpc_wait *, void *);

 



struct rpc_wait {
	struct rpc_sock *	w_sock;
	struct rpc_wait *	w_prev;
	struct rpc_wait *	w_next;
	struct rpc_ioreq *	w_req;
	int			w_result;
	struct wait_queue *	w_wait;
	rpc_callback_fn_t	w_handler;
	void *			w_cdata;
	char			w_queued;
	char			w_gotit;
	__u32			w_xid;
};

struct rpc_sock {
	struct file *		file;
	struct socket *		sock;
	struct sock *		inet;
	struct rpc_wait		waiting[32 ];
	unsigned long		cong;
	unsigned long		cwnd;
	struct rpc_wait *	pending;
	struct rpc_wait *	free;
	struct wait_queue *	backlog;
	struct wait_queue *	shutwait;
	int			shutdown;
};



 
int			rpc_call(struct rpc_sock *, struct rpc_ioreq *,
					 struct rpc_timeout *);
 



int			rpc_reserve(struct rpc_sock *, struct rpc_ioreq *, int);
void			rpc_release(struct rpc_sock *, struct rpc_ioreq *);
int			rpc_transmit(struct rpc_sock *, struct rpc_ioreq *);
int			rpc_doio(struct rpc_sock *, struct rpc_ioreq *,
					 struct rpc_timeout *, int);
struct rpc_sock	*	rpc_makesock(struct file *);
int			rpc_closesock(struct rpc_sock *);




# 4 "../../include/linux/nfs_fs_sb.h" 2



struct nfs_server {
	struct file *file;
	struct rpc_sock *rsock;
	struct sockaddr toaddr ;   
	int lock;
	struct wait_queue *wait;
	int flags;
	int rsize;
	int wsize;
	int timeo;
	int retrans;
	int acregmin;
	int acregmax;
	int acdirmin;
	int acdirmax;
	char hostname[256];
};

 



struct nfs_sb_info {
	struct nfs_server s_server;
	struct nfs_fh s_root;
};


# 471 "../../include/linux/fs.h" 2

# 1 "../../include/linux/xia_fs_sb.h" 1



 











struct xiafs_sb_info {
    u_long   s_nzones;
    u_long   s_ninodes;
    u_long   s_ndatazones;
    u_long   s_imap_zones;
    u_long   s_zmap_zones;
    u_long   s_firstdatazone;
    u_long   s_zone_shift;
    u_long   s_max_size;				 
    struct buffer_head * s_imap_buf[8 ];	 
    struct buffer_head * s_zmap_buf[32 ];	 
    int      s_imap_iznr[8 ];		 
    int      s_zmap_zznr[32 ];		 
    u_char   s_imap_cached;			 
    u_char   s_zmap_cached;			 
};





# 472 "../../include/linux/fs.h" 2

# 1 "../../include/linux/sysv_fs_sb.h" 1



 








struct sysv_sb_info {
	int	       s_type;		 
	unsigned int   s_block_size;	 
	unsigned int   s_block_size_1;	 
	unsigned int   s_block_size_bits;	 
	unsigned int   s_block_size_ratio;	 
	unsigned int   s_block_size_ratio_bits;	 
	char	       s_convert;	 
	char	       s_kludge_symlinks;  
	char	       s_truncate;	 
					 
	nlink_t        s_link_max;	 
	unsigned int   s_inodes_per_block;	 
	unsigned int   s_inodes_per_block_1;	 
	unsigned int   s_inodes_per_block_bits;	 
	unsigned int   s_ind_per_block;		 
	unsigned int   s_ind_per_block_1;	 
	unsigned int   s_ind_per_block_bits;	 
	unsigned int   s_ind_per_block_2;	 
	unsigned int   s_ind_per_block_2_1;	 
	unsigned int   s_ind_per_block_2_bits;	 
	unsigned int   s_ind_per_block_3;	 
	unsigned int   s_ind_per_block_block_size_1;	 
	unsigned int   s_ind_per_block_block_size_bits;	 
	unsigned int   s_ind_per_block_2_block_size_1;	 
	unsigned int   s_ind_per_block_2_block_size_bits;  
	unsigned int   s_ind0_size;		 
	unsigned int   s_ind1_size;		 
	unsigned int   s_ind2_size;		 
	unsigned int   s_toobig_block;		 
	unsigned int   s_block_base;	 
	unsigned short s_fic_size;	 
	unsigned short s_flc_size;	 
	 
	struct buffer_head *s_bh1;
	struct buffer_head *s_bh2;
	 

	char *         s_sbd1;		 
	char *         s_sbd2;		 
	unsigned short *s_sb_fic_count;	 
        unsigned short *s_sb_fic_inodes;  
	unsigned short *s_sb_total_free_inodes;  
	unsigned short *s_sb_flc_count;	 
	unsigned long  *s_sb_flc_blocks;  
	unsigned long  *s_sb_total_free_blocks; 
	unsigned long  *s_sb_time;	 
	unsigned long  *s_sb_state;	 
	 

	unsigned long  s_firstinodezone;  
	unsigned long  s_firstdatazone;	 
	unsigned long  s_ninodes;	 
	unsigned long  s_ndatazones;	 
	unsigned long  s_nzones;	 
};
 

 



















































# 473 "../../include/linux/fs.h" 2

# 1 "../../include/linux/affs_fs_sb.h" 1



 











struct affs_bm_info {
	struct buffer_head *bm_bh;	 
	int bm_firstblk;		 
	int bm_key;			 
	int bm_count;			 
};

struct affs_alloc_zone {
	short az_size;			 
	short az_count;			 
	int az_free;			 
};

struct affs_zone {
	unsigned long z_ino;		 
	struct affs_bm_info *z_bm;	 
	int z_start;			 
	int z_end;			 
	int z_az_no;			 
	unsigned long z_lru_time;	 
};

struct affs_sb_info {
	int s_partition_size;		 
	int s_root_block;		 
	int s_hashsize;			 
	unsigned long s_flags;		 
	short s_uid;			 
	short s_gid;			 
	umode_t s_mode;			 
	int s_reserved;			 
	struct buffer_head *s_root_bh;	 
	struct affs_bm_info *s_bitmap;	 
	int s_bm_count;			 
	int s_nextzone;			 
	int s_num_az;			 
	struct affs_zone *s_zones;	 
	struct affs_alloc_zone *s_alloc; 
	char *s_zonemap;		 
	char *s_prefix;			 
	int s_prefix_len;		 
	char s_volume[32];		 
};














# 474 "../../include/linux/fs.h" 2

# 1 "../../include/linux/ufs_fs_sb.h" 1
 















struct ufs_sb_info {
	struct ufs_superblock * s_raw_sb;
	__u32	s_flags;  
	__u32	s_ncg;	  
	__u32	s_ipg;	  
	__u32	s_fpg;
	__u32	s_fsize;
	__u32	s_bsize;
	__u32	s_iblkno;
	__u32	s_dblkno;
	__u32	s_cgoffset;
	__u32	s_cgmask;
	__u32	s_inopb;
	__u32	s_fsfrag;
};



 








# 475 "../../include/linux/fs.h" 2

# 1 "../../include/linux/romfs_fs_sb.h" 1



 

struct romfs_sb_info {
	unsigned long s_maxsize;
};


# 476 "../../include/linux/fs.h" 2


struct super_block {
	kdev_t s_dev;
	unsigned long s_blocksize;
	unsigned char s_blocksize_bits;
	unsigned char s_lock;
	unsigned char s_rd_only;
	unsigned char s_dirt;
	struct file_system_type *s_type;
	struct super_operations *s_op;
	struct dquot_operations *dq_op;
	unsigned long s_flags;
	unsigned long s_magic;
	unsigned long s_time;
	struct inode * s_covered;
	struct inode * s_mounted;
	struct wait_queue * s_wait;
	union {
		struct minix_sb_info minix_sb;
		struct ext_sb_info ext_sb;
		struct ext2_sb_info ext2_sb;
		struct hpfs_sb_info hpfs_sb;
		struct msdos_sb_info msdos_sb;
		struct isofs_sb_info isofs_sb;
		struct nfs_sb_info nfs_sb;
		struct xiafs_sb_info xiafs_sb;
		struct sysv_sb_info sysv_sb;
		struct affs_sb_info affs_sb;
		struct ufs_sb_info ufs_sb;
		struct romfs_sb_info romfs_sb;
		void *generic_sbp;
	} u;
};

 





typedef int (*filldir_t)(void *, const char *, int, off_t, ino_t);
	
struct file_operations {
	int (*lseek) (struct inode *, struct file *, off_t, int);
	int (*read) (struct inode *, struct file *, char *, int);
	int (*write) (struct inode *, struct file *, const char *, int);
	int (*readdir) (struct inode *, struct file *, void *, filldir_t);
	int (*select) (struct inode *, struct file *, int, select_table *);
	int (*ioctl) (struct inode *, struct file *, unsigned int, unsigned long);
	int (*mmap) (struct inode *, struct file *, struct vm_area_struct *);
	int (*open) (struct inode *, struct file *);
	void (*release) (struct inode *, struct file *);
	int (*fsync) (struct inode *, struct file *);
	int (*fasync) (struct inode *, struct file *, int);
	int (*check_media_change) (kdev_t dev);
	int (*revalidate) (kdev_t dev);



};

struct inode_operations {
	struct file_operations * default_file_ops;
	int (*create) (struct inode *,const char *,int,int,struct inode **);
	int (*lookup) (struct inode *,const char *,int,struct inode **);
	int (*link) (struct inode *,struct inode *,const char *,int);
	int (*unlink) (struct inode *,const char *,int);
	int (*symlink) (struct inode *,const char *,int,const char *);
	int (*mkdir) (struct inode *,const char *,int,int);
	int (*rmdir) (struct inode *,const char *,int);
	int (*mknod) (struct inode *,const char *,int,int,int);
	int (*rename) (struct inode *,const char *,int,struct inode *,const char *,int, int);
	int (*readlink) (struct inode *,char *,int);
	int (*follow_link) (struct inode *,struct inode *,int,int,struct inode **);
	int (*readpage) (struct inode *, struct page *);
	int (*writepage) (struct inode *, struct page *);
	int (*bmap) (struct inode *,int);
	void (*truncate) (struct inode *);
	int (*permission) (struct inode *, int);
	int (*smap) (struct inode *,int);
};

struct super_operations {
	void (*read_inode) (struct inode *);
	int (*notify_change) (struct inode *, struct iattr *);
	void (*write_inode) (struct inode *);
	void (*put_inode) (struct inode *);
	void (*put_super) (struct super_block *);
	void (*write_super) (struct super_block *);
	void (*statfs) (struct super_block *, struct statfs *, int);
	int (*remount_fs) (struct super_block *, int *, char *);
};

struct dquot_operations {
	void (*initialize) (struct inode *, short);
	void (*drop) (struct inode *);
	int (*alloc_block) (const struct inode *, unsigned long);
	int (*alloc_inode) (const struct inode *, unsigned long);
	void (*free_block) (const struct inode *, unsigned long);
	void (*free_inode) (const struct inode *, unsigned long);
	int (*transfer) (struct inode *, struct iattr *, char);
};

struct file_system_type {
	struct super_block *(*read_super) (struct super_block *, void *, int);
	const char *name;
	int requires_dev;
	struct file_system_type * next;
};

extern int register_filesystem(struct file_system_type *);
extern int unregister_filesystem(struct file_system_type *);

  int sys_open(const char *, int, int);
  int sys_close(unsigned int);		 
  int sys_read(unsigned int, char *, int);

extern void kill_fasync(struct fasync_struct *fa, int sig);

extern int getname(const char * filename, char **result);
extern void putname(char * name);
extern int do_truncate(struct inode *, unsigned long);
extern int register_blkdev(unsigned int, const char *, struct file_operations *);
extern int unregister_blkdev(unsigned int major, const char * name);
extern int blkdev_open(struct inode * inode, struct file * filp);
extern void blkdev_release (struct inode * inode);
extern struct file_operations def_blk_fops;
extern struct inode_operations blkdev_inode_operations;

extern int register_chrdev(unsigned int, const char *, struct file_operations *);
extern int unregister_chrdev(unsigned int major, const char * name);
extern int chrdev_open(struct inode * inode, struct file * filp);
extern struct file_operations def_chr_fops;
extern struct inode_operations chrdev_inode_operations;

extern void init_fifo(struct inode * inode);
extern struct inode_operations fifo_inode_operations;

extern struct file_operations connecting_fifo_fops;
extern struct file_operations read_fifo_fops;
extern struct file_operations write_fifo_fops;
extern struct file_operations rdwr_fifo_fops;
extern struct file_operations read_pipe_fops;
extern struct file_operations write_pipe_fops;
extern struct file_operations rdwr_pipe_fops;

extern struct file_system_type *get_fs_type(const char *name);

extern int fs_may_mount(kdev_t dev);
extern int fs_may_umount(kdev_t dev, struct inode * mount_root);
extern int fs_may_remount_ro(kdev_t dev);

extern struct file *first_file;
extern struct super_block super_blocks[8 ];

extern void refile_buffer(struct buffer_head * buf);
extern void set_writetime(struct buffer_head * buf, int flag);
extern int try_to_free_buffer(struct buffer_head*, struct buffer_head**, int);

extern int nr_buffers;
extern int buffermem;
extern int nr_buffer_heads;







void mark_buffer_uptodate(struct buffer_head * bh, int on);

extern inline void mark_buffer_clean(struct buffer_head * bh)
{
	if (clear_bit(1 , &bh->b_state)) {
		if (bh->b_list == 3 )
			refile_buffer(bh);
	}
}

extern inline void mark_buffer_dirty(struct buffer_head * bh, int flag)
{
	if (!set_bit(1 , &bh->b_state)) {
		set_writetime(bh, flag);
		if (bh->b_list != 3 )
			refile_buffer(bh);
	}
}

extern int check_disk_change(kdev_t dev);
extern void invalidate_inodes(kdev_t dev);
extern void invalidate_inode_pages(struct inode *);
extern void invalidate_buffers(kdev_t dev);
extern void invalidate_buffers_by_block(kdev_t dev, unsigned int start, unsigned int count);
extern void invalidate_buffers_by_byte(kdev_t dev, unsigned long pos, unsigned long len);
extern int floppy_is_wp(int minor);
extern void sync_inodes(kdev_t dev);
extern void sync_dev(kdev_t dev);
extern int fsync_dev(kdev_t dev);
extern void sync_supers(kdev_t dev);
extern int bmap(struct inode * inode,int block);
extern int notify_change(struct inode *, struct iattr *);
extern int namei(const char * pathname, struct inode ** res_inode);
extern int lnamei(const char * pathname, struct inode ** res_inode);
extern int permission(struct inode * inode,int mask);
extern int get_write_access(struct inode *inode);
extern void put_write_access(struct inode *inode);
extern int open_namei(const char * pathname, int flag, int mode,
	struct inode ** res_inode, struct inode * base);
extern int do_mknod(const char * filename, int mode, dev_t dev);
extern int do_pipe(int *);
extern void iput(struct inode * inode);
extern struct inode * __iget(struct super_block * sb,int nr,int crsmnt);
extern struct inode * get_empty_inode(void);
extern void insert_inode_hash(struct inode *);
extern void clear_inode(struct inode *);
extern struct inode * get_pipe_inode(void);
extern void make_bad_inode(struct inode *);
extern int get_unused_fd(void);
extern void put_unused_fd(int);
extern struct file * get_empty_filp(void);
extern int close_fp(struct file *filp);
extern struct buffer_head * get_hash_table(kdev_t dev, int block, int size);
extern struct buffer_head * getblk(kdev_t dev, int block, int size);
extern void ll_rw_block(int rw, int nr, struct buffer_head * bh[]);
extern void ll_rw_page(int rw, kdev_t dev, unsigned long nr, char * buffer);
extern void ll_rw_swap_file(int rw, kdev_t dev, unsigned int *b, int nb, char *buffer);
extern int is_read_only(kdev_t dev);
extern void __brelse(struct buffer_head *buf);
extern inline void brelse(struct buffer_head *buf)
{
	if (buf)
		__brelse(buf);
}
extern void __bforget(struct buffer_head *buf);

extern inline void bforget(struct buffer_head *buf)
{
	if (buf)
		__bforget(buf);
}




extern void set_blocksize(kdev_t dev, int size);
extern struct buffer_head * bread(kdev_t dev, int block, int size);
extern struct buffer_head * breada(kdev_t dev,int block, int size, 
				   unsigned int pos, unsigned int filesize);

extern int generic_readpage(struct inode *, struct page *);
extern int generic_file_read(struct inode *, struct file *, char *, int);
extern int generic_file_mmap(struct inode *, struct file *, struct vm_area_struct *);
extern int brw_page(int, struct page *, kdev_t, int [], int, int);





extern void put_super(kdev_t dev);
unsigned long generate_cluster(kdev_t dev, int b[], int size);
unsigned long generate_cluster_swab32(kdev_t dev, int b[], int size);
extern kdev_t ROOT_DEV;

extern void show_buffers(void);
extern void mount_root(void);






extern int char_read(struct inode *, struct file *, char *, int);
extern int block_read(struct inode *, struct file *, char *, int);
extern int read_ahead[];

extern int char_write(struct inode *, struct file *, const char *, int);
extern int block_write(struct inode *, struct file *, const char *, int);

extern int block_fsync(struct inode *, struct file *);
extern int file_fsync(struct inode *, struct file *);

extern void dcache_add(struct inode *, const char *, int, unsigned long);
extern int dcache_lookup(struct inode *, const char *, int, unsigned long *);

extern int inode_change_ok(struct inode *, struct iattr *);
extern void inode_setattr(struct inode *, struct iattr *);

extern inline struct inode * iget(struct super_block * sb,int nr)
{
	return __iget(sb, nr, 1);
}

 
# 1 "../../include/linux/minix_fs.h" 1



 



 







 



















 



struct minix_inode {
	__u16 i_mode;
	__u16 i_uid;
	__u32 i_size;
	__u32 i_time;
	__u8  i_gid;
	__u8  i_nlinks;
	__u16 i_zone[9];
};

 





struct minix2_inode {
	__u16 i_mode;
	__u16 i_nlinks;
	__u16 i_uid;
	__u16 i_gid;
	__u32 i_size;
	__u32 i_atime;
	__u32 i_mtime;
	__u32 i_ctime;
	__u32 i_zone[10];
};

 


struct minix_super_block {
	__u16 s_ninodes;
	__u16 s_nzones;
	__u16 s_imap_blocks;
	__u16 s_zmap_blocks;
	__u16 s_firstdatazone;
	__u16 s_log_zone_size;
	__u32 s_max_size;
	__u16 s_magic;
	__u16 s_state;
	__u32 s_zones;
};

struct minix_dir_entry {
	__u16 inode;
	char name[0];
};



extern int minix_lookup(struct inode * dir,const char * name, int len,
                        struct inode ** result);
extern int minix_create(struct inode * dir,const char * name, int len, int mode,
	struct inode ** result);
extern int minix_mkdir(struct inode * dir, const char * name, int len, int mode);
extern int minix_rmdir(struct inode * dir, const char * name, int len);
extern int minix_unlink(struct inode * dir, const char * name, int len);
extern int minix_symlink(struct inode * inode, const char * name, int len,
	const char * symname);
extern int minix_link(struct inode * oldinode, struct inode * dir, const char * name, int len);
extern int minix_mknod(struct inode * dir, const char * name, int len, int mode, int rdev);
extern int minix_rename(struct inode * old_dir, const char * old_name, int old_len,
	struct inode * new_dir, const char * new_name, int new_len, int must_be_dir);
extern struct inode * minix_new_inode(const struct inode * dir);
extern void minix_free_inode(struct inode * inode);
extern unsigned long minix_count_free_inodes(struct super_block *sb);
extern int minix_new_block(struct super_block * sb);
extern void minix_free_block(struct super_block * sb, int block);
extern unsigned long minix_count_free_blocks(struct super_block *sb);

extern int minix_bmap(struct inode *,int);

extern struct buffer_head * minix_getblk(struct inode *, int, int);
extern struct buffer_head * minix_bread(struct inode *, int, int);

extern void minix_truncate(struct inode *);
extern void minix_put_super(struct super_block *);
extern struct super_block *minix_read_super(struct super_block *,void *,int);
extern int init_minix_fs(void);
extern void minix_write_super(struct super_block *);
extern int minix_remount (struct super_block * sb, int * flags, char * data);
extern void minix_read_inode(struct inode *);
extern void minix_write_inode(struct inode *);
extern void minix_put_inode(struct inode *);
extern void minix_statfs(struct super_block *, struct statfs *, int);
extern int minix_sync_inode(struct inode *);
extern int minix_sync_file(struct inode *, struct file *);

extern struct inode_operations minix_file_inode_operations;
extern struct inode_operations minix_dir_inode_operations;
extern struct inode_operations minix_symlink_inode_operations;




# 770 "../../include/linux/fs.h" 2



 



# 788 "../../include/linux/fs.h"





# 19 "../../include/linux/tty.h" 2

# 1 "../../include/linux/termios.h" 1




# 1 "../../include/asm/termios.h" 1



# 1 "../../include/asm/termbits.h" 1





typedef unsigned char	cc_t;
typedef unsigned int	speed_t;
typedef unsigned int	tcflag_t;


struct termios {
	tcflag_t c_iflag;		 
	tcflag_t c_oflag;		 
	tcflag_t c_cflag;		 
	tcflag_t c_lflag;		 
	cc_t c_line;			 
	cc_t c_cc[19 ];		 
};

 



















 















 
































 













































 

















 





 




 





# 4 "../../include/asm/termios.h" 2

# 1 "../../include/asm/ioctls.h" 1





 


























































 











# 5 "../../include/asm/termios.h" 2

 
struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};


struct termio {
	unsigned short c_iflag;		 
	unsigned short c_oflag;		 
	unsigned short c_cflag;		 
	unsigned short c_lflag;		 
	unsigned char c_line;		 
	unsigned char c_cc[8 ];	 
};


 








 












 

 








 


extern inline void trans_from_termio(struct termio * termio,
	struct termios * termios)
{

	(( termios->c_iflag ) = (0xffff0000 & ( termios->c_iflag )) | (  termio->c_iflag )) ;
	(( termios->c_oflag ) = (0xffff0000 & ( termios->c_oflag )) | (  termio->c_oflag )) ;
	(( termios->c_cflag ) = (0xffff0000 & ( termios->c_cflag )) | (  termio->c_cflag )) ;
	(( termios->c_lflag ) = (0xffff0000 & ( termios->c_lflag )) | (  termio->c_lflag )) ;

	memcpy(termios->c_cc, termio->c_cc, 8 );
}

 


extern inline void trans_to_termio(struct termios * termios,
	struct termio * termio)
{
	termio->c_iflag = termios->c_iflag;
	termio->c_oflag = termios->c_oflag;
	termio->c_cflag = termios->c_cflag;
	termio->c_lflag = termios->c_lflag;
	termio->c_line	= termios->c_line;
	memcpy(termio->c_cc, termios->c_cc, 8 );
}




# 5 "../../include/linux/termios.h" 2



# 20 "../../include/linux/tty.h" 2

# 1 "../../include/linux/tqueue.h" 1
 

















 






















struct tq_struct {
	struct tq_struct *next;		 
	int sync;			 
	void (*routine)(void *);	 
	void *data;			 
};

typedef struct tq_struct * task_queue;



extern task_queue tq_timer, tq_immediate, tq_scheduler, tq_disk;

 





















 




extern __inline__ void queue_task_irq(struct tq_struct *bh_pointer,
			       task_queue *bh_list)
{
	if (!set_bit(0,&bh_pointer->sync)) {
		bh_pointer->next = *bh_list;
		*bh_list = bh_pointer;
	}
}

 



extern __inline__ void queue_task_irq_off(struct tq_struct *bh_pointer,
				 task_queue *bh_list)
{
	if (!(bh_pointer->sync & 1)) {
		bh_pointer->sync = 1;
		bh_pointer->next = *bh_list;
		*bh_list = bh_pointer;
	}
}


 


extern __inline__ void queue_task(struct tq_struct *bh_pointer,
			   task_queue *bh_list)
{
	if (!set_bit(0,&bh_pointer->sync)) {
		unsigned long flags;
		(( flags ) = __st20_gintdis(0)) ;
		((void) __st20_gintdis(7UL )) ;
		bh_pointer->next = *bh_list;
		*bh_list = bh_pointer;
		do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
	}
}

 


extern __inline__ void run_task_queue(task_queue *list)
{
	struct tq_struct *p;

	p = ((__typeof__(*( list )))__xchg((unsigned long)( ((void *) 0)  ),( list ),sizeof(*( list )))) ;
	while (p) {
		void *arg;
		void (*f) (void *);
		struct tq_struct *save_p;
		arg    = p -> data;
		f      = p -> routine;
		save_p = p;
		p      = p -> next;
		save_p -> sync = 0;
		(*f)(arg);
	}
}


# 21 "../../include/linux/tty.h" 2

# 1 "../../include/linux/tty_driver.h" 1



 






























































































struct tty_driver {
	int	magic;		 
	const char	*name;
	int	name_base;	 
	short	major;		 
	short	minor_start;	 
	short	num;		 
	short	type;		 
	short	subtype;	 
	struct termios init_termios;  
	int	flags;		 
	int	*refcount;	 
	struct tty_driver *other;  

	 


	struct tty_struct **table;
	struct termios **termios;
	struct termios **termios_locked;
	
	 



	int  (*open)(struct tty_struct * tty, struct file * filp);
	void (*close)(struct tty_struct * tty, struct file * filp);
	int  (*write)(struct tty_struct * tty, int from_user,
		      const unsigned char *buf, int count);
	void (*put_char)(struct tty_struct *tty, unsigned char ch);
	void (*flush_chars)(struct tty_struct *tty);
	int  (*write_room)(struct tty_struct *tty);
	int  (*chars_in_buffer)(struct tty_struct *tty);
	int  (*ioctl)(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg);
	void (*set_termios)(struct tty_struct *tty, struct termios * old);
	void (*throttle)(struct tty_struct * tty);
	void (*unthrottle)(struct tty_struct * tty);
	void (*stop)(struct tty_struct *tty);
	void (*start)(struct tty_struct *tty);
	void (*hangup)(struct tty_struct *tty);
	void (*flush_buffer)(struct tty_struct *tty);
	void (*set_ldisc)(struct tty_struct *tty);

	 


	struct tty_driver *next;
	struct tty_driver *prev;
};

 


 




















 






 



 




# 22 "../../include/linux/tty.h" 2

# 1 "../../include/linux/tty_ldisc.h" 1



 






struct tty_ldisc {
	int	magic;
	int	num;
	int	flags;
	 


	int	(*open)(struct tty_struct *);
	void	(*close)(struct tty_struct *);
	void	(*flush_buffer)(struct tty_struct *tty);
	int	(*chars_in_buffer)(struct tty_struct *tty);
	int	(*read)(struct tty_struct * tty, struct file * file,
			unsigned char * buf, unsigned int nr);
	int	(*write)(struct tty_struct * tty, struct file * file,
			 const unsigned char * buf, unsigned int nr);	
	int	(*ioctl)(struct tty_struct * tty, struct file * file,
			 unsigned int cmd, unsigned long arg);
	void	(*set_termios)(struct tty_struct *tty, struct termios * old);
	int	(*select)(struct tty_struct * tty, struct inode * inode,
			  struct file * file, int sel_type,
			  struct select_table_struct *wait);
	
	 


	void	(*receive_buf)(struct tty_struct *, const unsigned char *cp,
			       char *fp, int count);
	int	(*receive_room)(struct tty_struct *);
	void	(*write_wakeup)(struct tty_struct *);
};






# 23 "../../include/linux/tty.h" 2





 








 



struct screen_info {
	unsigned char  orig_x;
	unsigned char  orig_y;
	unsigned char  unused1[2];
	unsigned short orig_video_page;
	unsigned char  orig_video_mode;
	unsigned char  orig_video_cols;
	unsigned short unused2;
	unsigned short orig_video_ega_bx;
	unsigned short unused3;
	unsigned char  orig_video_lines;
	unsigned char  orig_video_isVGA;
	unsigned short orig_video_points;
};

extern struct screen_info screen_info;




















 






 






struct tty_flip_buffer {
	struct tq_struct tqueue;
	unsigned char	char_buf[2* 512 ];
	char		flag_buf[2* 512 ];
	char		*char_buf_ptr;
	unsigned char	*flag_buf_ptr;
	int		count;
	int		buf_num;
};

 

























































































 













struct tty_struct {
	int	magic;
	struct tty_driver driver;
	struct tty_ldisc ldisc;
	struct termios *termios, *termios_locked;
	int pgrp;
	int session;
	kdev_t	device;
	unsigned long flags;
	int count;
	struct winsize winsize;
	unsigned char stopped:1, hw_stopped:1, packet:1;
	unsigned char ctrl_status;

	struct tty_struct *link;
	struct fasync_struct *fasync;
	struct tty_flip_buffer flip;
	int max_flip_cnt;
	struct wait_queue *write_wait;
	struct wait_queue *read_wait;
	void *disc_data;
	void *driver_data;


	
	 



	unsigned int column;
	unsigned char lnext:1, erasing:1, raw:1, real_raw:1, icanon:1;
	unsigned char closing:1;
	unsigned short minimum_to_wake;
	unsigned overrun_time;
	int num_overrun;
	unsigned long process_char_map[256/(8*sizeof(unsigned long))];
	char *read_buf;
	int read_head;
	int read_tail;
	int read_cnt;
	unsigned long read_flags[4096 /(8*sizeof(unsigned long))];
	int canon_data;
	unsigned long canon_head;
	unsigned int canon_column;
};

 


 


















extern void tty_write_flush(struct tty_struct *);

extern struct termios tty_std_termios;
extern struct tty_struct * redirect;
extern struct tty_ldisc ldiscs[];
extern int fg_console, last_console, want_console;

extern int kmsg_redirect;
extern struct wait_queue * keypress_wait;

extern unsigned long con_init(unsigned long);

extern int fbmem_init(void);
extern int rs_init(void);
extern int lp_init(void);
extern int pty_init(void);
extern int tty_init(void);
extern int pcxe_init(void);
extern int vcs_init(void);
extern int cy_init(void);
extern int stl_init(void);
extern int stli_init(void);
extern int riscom8_init(void);
extern int specialix_init(void);
extern int baycom_init(void);
extern int mcfrs_init(void);
extern int rs68328_init(void);
extern int rs68332_init(void);
extern int lc7981_init(void);
extern int tpu_68332_init(void);
extern int portf_68332_init(void);
extern int porte_68332_init(void);
extern int rsmax311x_init(void);

extern int tty_paranoia_check(struct tty_struct *tty, kdev_t device,
			      const char *routine);
extern char *_tty_name(struct tty_struct *tty, char *buf);
extern char *tty_name(struct tty_struct *tty);
extern void tty_wait_until_sent(struct tty_struct * tty, int timeout);
extern int tty_check_change(struct tty_struct * tty);
extern void stop_tty(struct tty_struct * tty);
extern void start_tty(struct tty_struct * tty);
extern int tty_register_ldisc(int disc, struct tty_ldisc *new_ldisc);
extern int tty_register_driver(struct tty_driver *driver);
extern int tty_unregister_driver(struct tty_driver *driver);
extern int tty_read_raw_data(struct tty_struct *tty, unsigned char *bufp,
			     int buflen);
extern void tty_write_message(struct tty_struct *tty, char *msg);

extern int is_orphaned_pgrp(int pgrp);
extern int is_ignored(int sig);
extern int tty_signal(int sig, struct tty_struct *tty);
extern void tty_hangup(struct tty_struct * tty);
extern void tty_vhangup(struct tty_struct * tty);
extern void tty_unhangup(struct file *filp);
extern int tty_hung_up_p(struct file * filp);
extern void do_SAK(struct tty_struct *tty);
extern void disassociate_ctty(int priv);

 
extern struct tty_ldisc tty_ldisc_N_TTY;

 
extern int n_tty_ioctl(struct tty_struct * tty, struct file * file,
		       unsigned int cmd, unsigned long arg);

 

extern int  rs_open(struct tty_struct * tty, struct file * filp);

 

extern int  pty_open(struct tty_struct * tty, struct file * filp);
extern int pcxe_open(struct tty_struct *tty, struct file *filp);

 

extern int con_open(struct tty_struct * tty, struct file * filp);
extern void update_screen(int new_console);
extern void console_print(const char *);

 

extern int vt_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg);



# 31 "../../include/linux/sched.h" 2

# 1 "../../include/linux/sem.h" 1


# 1 "../../include/linux/ipc.h" 1




typedef int key_t; 		 


struct ipc_perm
{
  key_t  key;
  ushort uid;    
  ushort gid;
  ushort cuid;   
  ushort cgid;
  ushort mode;   
  ushort seq;    
};


 





 










 



 


struct ipc_kludge {
    struct msgbuf *msgp;
    long msgtyp;
};




















# 3 "../../include/linux/sem.h" 2


 


 








 
struct semid_ds {
  struct ipc_perm sem_perm;             
  time_t          sem_otime;            
  time_t          sem_ctime;            
  struct sem      *sem_base;            
  struct sem_queue *sem_pending;        
  struct sem_queue **sem_pending_last;  
  struct sem_undo *undo;	        
  ushort          sem_nsems;            
};

 
struct sembuf {
  ushort  sem_num;         
  short   sem_op;          
  short   sem_flg;         
};

 
union semun {
  int val;			 
  struct semid_ds *buf;		 
  ushort *array;		 
  struct seminfo *__buf;	 
  void *__pad;
};

struct  seminfo {
    int semmap;
    int semmni;
    int semmns;
    int semmnu;
    int semmsl;
    int semopm;
    int semume;
    int semusz;
    int semvmx;
    int semaem;
};







 








 
struct sem {
  short   semval;          
  short   sempid;          
};

 



 
struct sem_queue {
    struct sem_queue *	next;	  
    struct sem_queue **	prev;	  
    struct wait_queue *	sleeper;  
    struct sem_undo *	undo;	  
    int    		pid;	  
    int    		status;	  
    struct semid_ds *	sma;	  
    struct sembuf *	sops;	  
    int			nsops;	  
};

 


struct sem_undo {
    struct sem_undo *  proc_next;  
    struct sem_undo *  id_next;	   
    int		       semid;	   
    short *	       semadj;	   
};

  int sys_semget (key_t key, int nsems, int semflg);
  int sys_semop (int semid, struct sembuf *sops, unsigned nsops);
  int sys_semctl (int semid, int semnum, int cmd, union semun arg);




# 32 "../../include/linux/sched.h" 2


 











 









extern unsigned long avenrun[];		 
















extern int nr_running, nr_tasks;
extern int last_pid;




# 1 "../../include/linux/head.h" 1



typedef struct desc_struct {
	unsigned long a,b;
} desc_table[256];

extern desc_table idt,gdt;











# 79 "../../include/linux/sched.h" 2


# 1 "../../include/linux/signal.h" 1



# 1 "../../include/asm/signal.h" 1



typedef unsigned long sigset_t;		 

 


































 






 

















 













 
typedef void (*__sighandler_t)(int);





struct sigaction {
	__sighandler_t sa_handler;
	sigset_t sa_mask;
	unsigned long sa_flags;
	void (*sa_restorer)(void);
};


# 1 "../../include/asm/sigcontext.h" 1



struct sigcontext_struct {
	unsigned long  sc_mask; 	 
	unsigned long  sc_usp;		 
	unsigned long  sc_er0;
	unsigned long  sc_er1;
	unsigned long  sc_er2;
	unsigned short sc_sr;
	unsigned long  sc_pc;
};


# 95 "../../include/asm/signal.h" 2




# 4 "../../include/linux/signal.h" 2



# 81 "../../include/linux/sched.h" 2


# 1 "../../include/linux/param.h" 1






# 83 "../../include/linux/sched.h" 2

# 1 "../../include/linux/resource.h" 1





 



 










struct	rusage {
	struct timeval ru_utime;	 
	struct timeval ru_stime;	 
	long	ru_maxrss;		 
	long	ru_ixrss;		 
	long	ru_idrss;		 
	long	ru_isrss;		 
	long	ru_minflt;		 
	long	ru_majflt;		 
	long	ru_nswap;		 
	long	ru_inblock;		 
	long	ru_oublock;		 
	long	ru_msgsnd;		 
	long	ru_msgrcv;		 
	long	ru_nsignals;		 
	long	ru_nvcsw;		 
	long	ru_nivcsw;		 
};



struct rlimit {
	long	rlim_cur;
	long	rlim_max;
};








 



# 1 "../../include/asm/resource.h" 1



 



















# 36 "../../include/asm/resource.h"




# 58 "../../include/linux/resource.h" 2



# 84 "../../include/linux/sched.h" 2


# 1 "../../include/linux/timer.h" 1



 




 


















































struct timer_struct {
	unsigned long expires;
	void (*fn)(void);
};

extern unsigned long timer_active;
extern struct timer_struct timer_table[32];

 












struct timer_list {
	struct timer_list *next;
	struct timer_list *prev;
	unsigned long expires;
	unsigned long data;
	void (*function)(unsigned long);
};

extern void add_timer(struct timer_list * timer);
extern int  del_timer(struct timer_list * timer);

extern void it_real_fn(unsigned long);

extern inline void init_timer(struct timer_list * timer)
{
	timer->next = ((void *) 0) ;
	timer->prev = ((void *) 0) ;
}


# 86 "../../include/linux/sched.h" 2


# 1 "../../include/asm/processor.h" 1














 



struct thread_struct {
	unsigned long	ksp;		 
	unsigned long	usp;		 
	unsigned long	fs;		 
	struct pt_regs	*regs;		 
	unsigned long	vfork_stack;	 

	unsigned long	resume[5];	 

	int		resume_valid;	 
	unsigned long	saved_mask;	 
	int		saved_mask_valid;
};

extern unsigned long init_kernel_stack[];








 
 
extern void st20_start_thread(struct pt_regs * regs, unsigned long pc, unsigned long usp);

static inline void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long a5,
				unsigned long usp)
{
	set_fs(((1) ) );
	st20_start_thread(regs, pc, usp);
}

extern inline unsigned long thread_saved_pc(struct thread_struct *t)
{
	return t->ksp ? ((unsigned long *) t->ksp)[0] : 0;
}


# 88 "../../include/linux/sched.h" 2









 






struct sched_param {
	int sched_priority;
};







extern void sched_init(void);
extern void show_state(void);
extern void trap_init(void);

  void schedule(void);

 
struct files_struct {
	int count;
	fd_set close_on_exec;
	fd_set open_fds;
	struct file * fd[128 ];
};








struct fs_struct {
	int count;
	unsigned short umask;
	struct inode * root, * pwd;
};







# 172 "../../include/linux/sched.h"

struct mm_rblock_struct {
	int size;
	int refcount;
	void * kblock;
};
struct mm_tblock_struct {
	struct mm_rblock_struct * rblock;
	struct mm_tblock_struct * next;
};
struct mm_struct {
	int count;
	unsigned long context;
	unsigned long start_code, end_code, start_data, end_data;
	unsigned long start_brk, brk, end_brk, start_stack, start_mmap;
	unsigned long arg_start, arg_end, env_start, env_end;
	unsigned long rss, total_vm, locked_vm;
	unsigned long def_flags;
	struct wait_queue * vforkwait;
	struct inode * executable;
	struct mm_tblock_struct tblock;
};


# 206 "../../include/linux/sched.h"



struct signal_struct {
	int count;
	struct sigaction action[32  ];
};





struct task_struct {
 
	volatile long state;	 
	long counter;
	long priority;
	unsigned long signal;
	unsigned long blocked;	 
	unsigned long flags;	 
	int errno;
	long debugreg[8];   
	struct exec_domain *exec_domain;
 
	struct linux_binfmt *binfmt;
	struct task_struct *next_task, *prev_task;
	struct task_struct *next_run,  *prev_run;
	unsigned long saved_kernel_stack;
	unsigned long kernel_stack_page;
	int exit_code, exit_signal;
	 
	unsigned long personality;
	int dumpable:1;
	int did_exec:1;
	unsigned vforkwoken:1;	 
	 
	int pid;
	int pgrp;
	int tty_old_pgrp;
	int session;
	 
	int leader;
	int	groups[32 ];
	 




	struct task_struct *p_opptr, *p_pptr, *p_cptr, *p_ysptr, *p_osptr;
	struct wait_queue *wait_chldexit;	 
	unsigned short uid,euid,suid,fsuid;
	unsigned short gid,egid,sgid,fsgid;
	unsigned long timeout, policy, rt_priority;
	unsigned long it_real_value, it_prof_value, it_virt_value;
	unsigned long it_real_incr, it_prof_incr, it_virt_incr;
	struct timer_list real_timer;
	long utime, stime, cutime, cstime, start_time;
 
	unsigned long min_flt, maj_flt, nswap, cmin_flt, cmaj_flt, cnswap;
	int swappable:1;
	unsigned long swap_address;
	unsigned long old_maj_flt;	 
	unsigned long dec_flt;		 
	unsigned long swap_cnt;		 
 
	struct rlimit rlim[10 ];
	unsigned short used_math;
	char comm[16];
 
	int link_count;
	struct tty_struct *tty;  
 
	struct sem_undo *semundo;
	struct sem_queue *semsleeping;
 
	struct desc_struct *ldt;
 
	struct thread_struct tss;
 
	struct fs_struct *fs;
 
	struct files_struct *files;
 
	struct mm_struct *mm;
 
	struct signal_struct *sig;
 
	int priv, ppriv;





};

 



					 














 







 




# 361 "../../include/linux/sched.h"

extern struct   mm_struct init_mm;
extern struct task_struct init_task;
extern struct task_struct *task[512 ];
extern struct task_struct *last_task_used_math;
extern struct task_struct *current_set[1 ];
 




extern unsigned long volatile jiffies;
extern unsigned long itimer_ticks;
extern unsigned long itimer_next;
extern struct timeval xtime;
extern int need_resched;
extern void do_timer(struct pt_regs *);

extern unsigned int * prof_buffer;
extern unsigned long prof_len;
extern unsigned long prof_shift;

extern int securelevel;	 



extern void sleep_on(struct wait_queue ** p);
extern void interruptible_sleep_on(struct wait_queue ** p);
extern void wake_up(struct wait_queue ** p);
extern void wake_up_interruptible(struct wait_queue ** p);
extern void wake_up_process(struct task_struct * tsk);

extern void notify_parent(struct task_struct * tsk, int signal);
extern void force_sig(unsigned long sig,struct task_struct * p);
extern int send_sig(unsigned long sig,struct task_struct * p,int priv);
extern int in_group_p(gid_t grp);

extern int request_irq(unsigned int irq,
		       void (*handler)(int, void *, struct pt_regs *),
		       unsigned long flags, 
		       const char *device,
		       void *dev_id);
extern void free_irq(unsigned int irq, void *dev_id);

 





extern inline int suser(void)
{
	if ((0+current_set[0 ]) ->euid == 0) {
		(0+current_set[0 ]) ->flags |= 0x00000100 ;
		return 1;
	}
	return 0;
}

extern void copy_thread(int, unsigned long, unsigned long, struct task_struct *, struct pt_regs *);
extern void flush_thread(void);
extern void exit_thread(void);

extern void exit_mm(struct task_struct *);
extern void exit_fs(struct task_struct *);
extern void exit_files(struct task_struct *);
extern void exit_sighand(struct task_struct *);
extern void release_thread(struct task_struct *);

extern int do_execve(char *, char **, char **, struct pt_regs *);
extern int do_fork(unsigned long, unsigned long, struct pt_regs *);


 



extern inline struct file *file_from_fd(const unsigned int fd)
{

	if (fd >= 128 )
		return ((void *) 0) ;
	 
	return (0+current_set[0 ]) ->files->fd[fd];
}
	
 




extern inline void __add_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
	struct wait_queue *head = *p;
	struct wait_queue *next = ((struct wait_queue *)(( p )-1)) ;

	if (head)
		next = head;
	*p = wait;
	wait->next = next;
}

extern inline void add_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
	unsigned long flags;

	(( flags ) = __st20_gintdis(0)) ;
	((void) __st20_gintdis(7UL )) ;
	__add_wait_queue(p, wait);
	do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
}

extern inline void __remove_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
	struct wait_queue * next = wait->next;
	struct wait_queue * head = next;

	for (;;) {
		struct wait_queue * nextlist = head->next;
		if (nextlist == wait)
			break;
		head = nextlist;
	}
	head->next = next;
}

extern inline void remove_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
	unsigned long flags;

	(( flags ) = __st20_gintdis(0)) ;
	((void) __st20_gintdis(7UL )) ;
	__remove_wait_queue(p, wait);
	do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
}

extern inline void select_wait(struct wait_queue ** wait_address, select_table * p)
{
	struct select_table_entry * entry;

	if (!p || !wait_address)
		return;
	if (p->nr >= (4096 / sizeof (struct select_table_entry)) )
		return;
 	entry = p->entry + p->nr;
	entry->wait_address = wait_address;
	entry->wait.task = (0+current_set[0 ]) ;
	entry->wait.next = ((void *) 0) ;
	add_wait_queue(wait_address,&entry->wait);
	p->nr++;
}


# 525 "../../include/linux/sched.h"


# 538 "../../include/linux/sched.h"







# 12 "inode.c" 2



# 1 "../../include/linux/mm.h" 1









# 1 "../../include/linux/string.h" 1




# 1 "../../include/linux/autoconf.h" 1
 










 









 






 









 





 




# 5 "../../include/linux/string.h" 2











extern char * ___strtok;
extern char * strcpy(char *,const char *);
extern char * strncpy(char *,const char *, __kernel_size_t);
extern char * strcat(char *, const char *);
extern char * strncat(char *, const char *, __kernel_size_t);
extern char * strchr(const char *,int);
extern char * strrchr(const char *,int);
extern char * strpbrk(const char *,const char *);
extern char * strtok(char *,const char *);
extern char * strstr(const char *,const char *);
extern __kernel_size_t strlen(const char *);
extern __kernel_size_t strnlen(const char *,__kernel_size_t);
extern __kernel_size_t strspn(const char *,const char *);
extern int strcmp(const char *,const char *);
extern int strncmp(const char *,const char *,__kernel_size_t);


extern void * memset(void *,int,__kernel_size_t);
extern void * memcpy(void *,const void *,__kernel_size_t);
extern int memcmp(const void *,const void *,__kernel_size_t);

extern void * memscan(void *,int,__kernel_size_t);
extern void * memmove(void *,const void *,__kernel_size_t);

 


# 1 "../../include/asm/string.h" 1








# 43 "../../include/linux/string.h" 2







# 10 "../../include/linux/mm.h" 2


extern unsigned long high_memory;


# 1 "../../include/asm/atomic.h" 1





typedef int atomic_t;

static __inline__ void atomic_add(atomic_t i, atomic_t *v)
{
	unsigned long flags;
	(( flags ) = __st20_gintdis(0)) ; ((void) __st20_gintdis(7UL )) ; *v += i; do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
}

static __inline__ void atomic_sub(atomic_t i, atomic_t *v)
{
	unsigned long flags;
	(( flags ) = __st20_gintdis(0)) ; ((void) __st20_gintdis(7UL )) ; *v -= i; do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
}

static __inline__ void atomic_inc(atomic_t *v) { atomic_add(1, v); }
static __inline__ void atomic_dec(atomic_t *v) { atomic_sub(1, v); }

static __inline__ int atomic_dec_and_test(atomic_t *v)
{
	unsigned long flags;
	int c;
	(( flags ) = __st20_gintdis(0)) ; ((void) __st20_gintdis(7UL )) ; c = --*v; do { unsigned long __f = ( flags ) & 7UL ; (void) __st20_gintdis(7UL ); if (__f) (void) __st20_gintenb(__f); } while (0) ;
	return c == 0;
}


# 15 "../../include/linux/mm.h" 2












extern int verify_area(int, const void *, unsigned long);





static inline int is_in_rom(unsigned long addr) { return 0; }


 








# 77 "../../include/linux/mm.h"



 

   
struct vm_area_struct {
	unsigned long vm_start;
	unsigned long vm_end;
	unsigned short vm_flags;
	unsigned long vm_offset;
};



 






















# 143 "../../include/linux/mm.h"


 







typedef struct page {
	 
	struct page *next;
	struct page *prev;
	struct inode *inode;
	unsigned long offset;
	struct page *next_hash;
	atomic_t count;
	unsigned flags;	 
	unsigned dirty:16,
		 age:8;
	struct wait_queue *wait;
	struct page *prev_hash;
	struct buffer_head * buffers;
	unsigned long swap_unlock_entry;
	unsigned long map_nr;	 
} mem_map_t;

 










 











 


































































extern mem_map_t * mem_map;

# 299 "../../include/linux/mm.h"


 






extern unsigned long __get_free_pages(int priority, unsigned long gfporder, int dma);

extern inline unsigned long get_free_page(int priority)
{
	unsigned long page;

	page = __get_free_pages(( priority ),0,0) ;
	if (page)
		memset((void *) page, 0, (1UL << 12 ) );
	return page;
}

 


extern void free_pages(unsigned long addr, unsigned long order);
extern void __free_page(struct page *);



extern void show_free_areas(void);
extern unsigned long put_dirty_page(struct task_struct * tsk,unsigned long page,
	unsigned long address);


extern void free_page_tables(struct mm_struct * mm);
extern void clear_page_tables(struct task_struct * tsk);
extern int new_page_tables(struct task_struct * tsk);
extern int copy_page_tables(struct task_struct * to);

# 350 "../../include/linux/mm.h"


extern unsigned long paging_init(unsigned long start_mem, unsigned long end_mem);
extern void mem_init(unsigned long start_mem, unsigned long end_mem);
extern void show_mem(void);
extern void oom(struct task_struct * tsk);
extern void si_meminfo(struct sysinfo * val);

 

extern void * vmalloc(unsigned long size);
extern void * vremap(unsigned long offset, unsigned long size);
extern void vfree(void * addr);
extern int vread(char *buf, char *addr, int count);

 
# 381 "../../include/linux/mm.h"

extern unsigned long do_mmap(struct file * file, unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long off);
extern int do_munmap(unsigned long, size_t);

extern void exit_mmap(struct mm_struct *);








 
extern unsigned long page_unuse(unsigned long);
extern int shrink_mmap(int, int, int);
extern void truncate_inode_pages(struct inode *, unsigned long);









 






# 473 "../../include/linux/mm.h"





# 15 "inode.c" 2



# 1 "../../include/linux/locks.h" 1







# 1 "../../include/linux/pagemap.h" 1





 








# 1 "../../include/linux/swapctl.h" 1






 

 
enum RCL_POLICY {RCL_ROUND_ROBIN, RCL_BUFF_FIRST, RCL_PERSIST};

typedef struct swap_control_v5
{
	int	sc_max_page_age;
	int	sc_page_advance;
	int	sc_page_decline;
	int	sc_page_initial_age;
	int	sc_max_buff_age;
	int	sc_buff_advance;
	int	sc_buff_decline;
	int	sc_buff_initial_age;
	int	sc_age_cluster_fract;
	int	sc_age_cluster_min;
	int	sc_pageout_weight;
	int	sc_bufferout_weight;
	int 	sc_buffer_grace;
	int 	sc_nr_buffs_to_free;
	int 	sc_nr_pages_to_free;
	enum RCL_POLICY	sc_policy;
} swap_control_v5;
typedef struct swap_control_v5 swap_control_t;
extern swap_control_t swap_control;

typedef struct kswapd_control_v1
{
	int	maxpages;
	int	pages_buff;
	int	pages_shm;
	int	pages_mmap;
	int	pages_swap;
} kswapd_control_v1;
typedef kswapd_control_v1 kswapd_control_t;
extern kswapd_control_t kswapd_ctl;

typedef struct swapstat_v1
{
	int	wakeups;
	int	pages_reclaimed;
	int	pages_shm;
	int	pages_mmap;
	int	pages_swap;
} swapstat_v1;
typedef swapstat_v1 swapstat_t;
extern swapstat_t swapstats;

extern int min_free_pages, free_pages_low, free_pages_high;






 

 














 











 


static inline int AGE_CLUSTER_SIZE(int resources)
{
	int n = (resources * (swap_control.sc_age_cluster_fract) ) >> 10;
	if (n < (swap_control.sc_age_cluster_min) )
		return (swap_control.sc_age_cluster_min) ;
	else
		return n;
}

static inline void touch_page(struct page *page)
{
	if (page->age < ((swap_control.sc_max_page_age)  - (swap_control.sc_page_advance) ))
		page->age += (swap_control.sc_page_advance) ;
	else
		page->age = (swap_control.sc_max_page_age) ;
}

static inline void age_page(struct page *page)
{
	if (page->age > (swap_control.sc_page_decline) )
		page->age -= (swap_control.sc_page_decline) ;
	else
		page->age = 0;
}

static inline int age_of(unsigned long addr)
{
	return mem_map[((((unsigned long)( addr )) - 0xc0000000 ) >> 12 ) ].age;
}

static inline void set_page_new(unsigned long addr)
{
	mem_map[((((unsigned long)( addr )) - 0xc0000000 ) >> 12 ) ].age = (swap_control.sc_page_initial_age) ;
}




# 15 "../../include/linux/pagemap.h" 2


static inline unsigned long page_address(struct page * page)
{
	return 0xc0000000  + (1UL << 12 )  * page->map_nr;
}










extern unsigned long page_cache_size;  
extern struct page * page_hash_table[(1 << 6 ) ];

 





static inline unsigned long _page_hashfn(struct inode * inode, unsigned long offset)
{



	return (( (((unsigned long) inode)/(sizeof(struct inode) & ~ (sizeof(struct inode) - 1))) + (offset >> 12 )  )+(( (((unsigned long) inode)/(sizeof(struct inode) & ~ (sizeof(struct inode) - 1))) + (offset >> 12 )  )>> 6 ))  & ((1 << 6 ) -1);



}



static inline struct page * __find_page(struct inode * inode, unsigned long offset, struct page *page)
{
	goto inside;
	for (;;) {
		page = page->next_hash;
inside:
		if (!page)
			goto not_found;
		if (page->inode != inode)
			continue;
		if (page->offset == offset)
			break;
	}
	 
	atomic_inc(&page->count);
	set_bit(2 , &page->flags);
not_found:
	return page;
}

static inline struct page *find_page(struct inode * inode, unsigned long offset)
{
	return __find_page(inode, offset, * (page_hash_table+_page_hashfn( inode ,  offset )) );
}

static inline void remove_page_from_hash_queue(struct page * page)
{
	struct page **p;
	struct page *next_hash, *prev_hash;

	next_hash = page->next_hash;
	prev_hash = page->prev_hash;
	page->next_hash = ((void *) 0) ;
	page->prev_hash = ((void *) 0) ;
	if (next_hash)
		next_hash->prev_hash = prev_hash;
	if (prev_hash)
		prev_hash->next_hash = next_hash;
	p = (page_hash_table+_page_hashfn( page->inode , page->offset )) ;
	if (*p == page)
		*p = next_hash;
	page_cache_size--;
}

static inline void __add_page_to_hash_queue(struct page * page, struct page **p)
{
	page_cache_size++;
	set_bit(2 , &page->flags);
	page->age = (((swap_control.sc_page_initial_age) )+((swap_control.sc_page_advance) )) ;
	page->prev_hash = ((void *) 0) ;
	if ((page->next_hash = *p) != ((void *) 0) )
		page->next_hash->prev_hash = page;
	*p = page;
}

static inline void add_page_to_hash_queue(struct page * page, struct inode * inode, unsigned long offset)
{
	__add_page_to_hash_queue(page, (page_hash_table+_page_hashfn( inode , offset )) );
}


static inline void remove_page_from_inode_queue(struct page * page)
{
	struct inode * inode = page->inode;

	page->inode = ((void *) 0) ;
	inode->i_nrpages--;
	if (inode->i_pages == page)
		inode->i_pages = page->next;
	if (page->next)
		page->next->prev = page->prev;
	if (page->prev)
		page->prev->next = page->next;
	page->next = ((void *) 0) ;
	page->prev = ((void *) 0) ;
}

static inline void add_page_to_inode_queue(struct inode * inode, struct page * page)
{
	struct page **p = &inode->i_pages;

	inode->i_nrpages++;
	page->inode = inode;
	page->prev = ((void *) 0) ;
	if ((page->next = *p) != ((void *) 0) )
		page->next->prev = page;
	*p = page;
}

extern void __wait_on_page(struct page *);
static inline void wait_on_page(struct page * page)
{
	if ((test_bit(0 , &( page )->flags)) )
		__wait_on_page(page);
}

extern void update_vm_cache(struct inode *, unsigned long, const char *, int);


# 8 "../../include/linux/locks.h" 2



 



extern struct buffer_head *reuse_list;

 



extern void __wait_on_buffer(struct buffer_head *);

extern inline void wait_on_buffer(struct buffer_head * bh)
{
	if (test_bit(2 , &bh->b_state))
		__wait_on_buffer(bh);
}

extern inline void lock_buffer(struct buffer_head * bh)
{
	while (set_bit(2 , &bh->b_state))
		__wait_on_buffer(bh);
}

void unlock_buffer(struct buffer_head *);


 




extern void __wait_on_super(struct super_block *);

extern inline void wait_on_super(struct super_block * sb)
{
	if (sb->s_lock)
		__wait_on_super(sb);
}

extern inline void lock_super(struct super_block * sb)
{
	if (sb->s_lock)
		__wait_on_super(sb);
	sb->s_lock = 1;
}

extern inline void unlock_super(struct super_block * sb)
{
	sb->s_lock = 0;
	wake_up(&sb->s_wait);
}



# 18 "inode.c" 2






void minix_put_inode(struct inode *inode)
{
	if (inode->i_nlink)
		return;
	inode->i_size = 0;
	minix_truncate(inode);
	minix_free_inode(inode);
}

static void minix_commit_super (struct super_block * sb,
			       struct minix_super_block * ms)
{
	mark_buffer_dirty(sb->u.minix_sb.s_sbh, 1);
	sb->s_dirt = 0;
}

void minix_write_super (struct super_block * sb)
{
	struct minix_super_block * ms;

	if (!(sb->s_flags & 1 )) {
		ms = sb->u.minix_sb.s_ms;

		if (ms->s_state & 0x0001 )
			ms->s_state &= ~0x0001 ;
		minix_commit_super (sb, ms);
	}
	sb->s_dirt = 0;
}


void minix_put_super(struct super_block *sb)
{
	int i;

	lock_super(sb);
	if (!(sb->s_flags & 1 )) {
		sb->u.minix_sb.s_ms->s_state = sb->u.minix_sb.s_mount_state;
		mark_buffer_dirty(sb->u.minix_sb.s_sbh, 1);
	}
	sb->s_dev = 0;
	for(i = 0 ; i < 8  ; i++)
		brelse(sb->u.minix_sb.s_imap[i]);
	for(i = 0 ; i < 64  ; i++)
		brelse(sb->u.minix_sb.s_zmap[i]);
	brelse (sb->u.minix_sb.s_sbh);
	unlock_super(sb);
	do { } while (0) ;
	return;
}

static struct super_operations minix_sops = { 
	minix_read_inode,
	((void *) 0) ,
	minix_write_inode,
	minix_put_inode,
	minix_put_super,
	minix_write_super,
	minix_statfs,
	minix_remount
};

int minix_remount (struct super_block * sb, int * flags, char * data)
{
	struct minix_super_block * ms;

	ms = sb->u.minix_sb.s_ms;
	if ((*flags & 1 ) == (sb->s_flags & 1 ))
		return 0;
	if (*flags & 1 ) {
		if (ms->s_state & 0x0001  ||
		    !(sb->u.minix_sb.s_mount_state & 0x0001 ))
			return 0;
		 
		ms->s_state = sb->u.minix_sb.s_mount_state;
		mark_buffer_dirty(sb->u.minix_sb.s_sbh, 1);
		sb->s_dirt = 1;
		minix_commit_super (sb, ms);
	}
	else {
	  	 
		sb->u.minix_sb.s_mount_state = ms->s_state;
		ms->s_state &= ~0x0001 ;
		mark_buffer_dirty(sb->u.minix_sb.s_sbh, 1);
		sb->s_dirt = 1;

		if (!(sb->u.minix_sb.s_mount_state & 0x0001 ))
			printk ("MINIX-fs warning: remounting unchecked fs, "
				"running fsck is recommended.\n");
		else if ((sb->u.minix_sb.s_mount_state & 0x0002 ))
			printk ("MINIX-fs warning: remounting fs with errors, "
				"running fsck is recommended.\n");
	}
	return 0;
}

 




static const char * minix_checkroot(struct super_block *s)
{
	struct inode * dir;
	struct buffer_head *bh;
	struct minix_dir_entry *de;
	const char * errmsg;
	int dirsize;

	dir = s->s_mounted;
	if (! ((( dir->i_mode ) & 00170000 ) == 0040000 ) )
		return "root directory is not a directory";

	bh = minix_bread(dir, 0, 0);
	if (!bh)
		return "unable to read root directory";

	de = (struct minix_dir_entry *) bh->b_data;
	errmsg = "bad root directory '.' entry";
	dirsize = 1024 ;
	if (de->inode == 1  && strcmp(de->name, ".") == 0) {
		errmsg = "bad root directory '..' entry";
		dirsize = 8;
	}

	while ((dirsize <<= 1) < 1024 ) {
		de = (struct minix_dir_entry *) (bh->b_data + dirsize);
		if (de->inode != 1 )
			continue;
		if (strcmp(de->name, ".."))
			continue;
		s->u.minix_sb.s_dirsize = dirsize;
		s->u.minix_sb.s_namelen = dirsize - 2;
		errmsg = ((void *) 0) ;
		break;
	}
	brelse(bh);
	return errmsg;
}

struct super_block *minix_read_super(struct super_block *s,void *data, 
				     int silent)
{
	struct buffer_head *bh;
	struct minix_super_block *ms;
	int i, block;
	kdev_t dev = s->s_dev;
	const char * errmsg;

	if (32 != sizeof (struct minix_inode))
		panic("bad V1 i-node size");
	if (64 != sizeof(struct minix2_inode))
		panic("bad V2 i-node size");
	do { } while (0) ;
	lock_super(s);
	set_blocksize(dev, 1024 );
	if (!(bh = bread(dev,1,1024 ))) {
		s->s_dev = 0;
		unlock_super(s);
		printk("MINIX-fs: unable to read superblock\n");
		do { } while (0) ;
		return ((void *) 0) ;
	}
	ms = (struct minix_super_block *) bh->b_data;
	s->u.minix_sb.s_ms = ms;
	s->u.minix_sb.s_sbh = bh;
	s->u.minix_sb.s_mount_state = ms->s_state;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->u.minix_sb.s_ninodes = ms->s_ninodes;
	s->u.minix_sb.s_imap_blocks = ms->s_imap_blocks;
	s->u.minix_sb.s_zmap_blocks = ms->s_zmap_blocks;
	s->u.minix_sb.s_firstdatazone = ms->s_firstdatazone;
	s->u.minix_sb.s_log_zone_size = ms->s_log_zone_size;
	s->u.minix_sb.s_max_size = ms->s_max_size;
	s->s_magic = ms->s_magic;
	if (s->s_magic == 0x137F ) {
		s->u.minix_sb.s_version = 0x0001 ;
		s->u.minix_sb.s_nzones = ms->s_nzones;
		s->u.minix_sb.s_dirsize = 16;
		s->u.minix_sb.s_namelen = 14;
	} else if (s->s_magic == 0x138F ) {
		s->u.minix_sb.s_version = 0x0001 ;
		s->u.minix_sb.s_nzones = ms->s_nzones;
		s->u.minix_sb.s_dirsize = 32;
		s->u.minix_sb.s_namelen = 30;
	} else if (s->s_magic == 0x2468 ) {
		s->u.minix_sb.s_version = 0x0002 ;
		s->u.minix_sb.s_nzones = ms->s_zones;
		s->u.minix_sb.s_dirsize = 16;
		s->u.minix_sb.s_namelen = 14;
	} else if (s->s_magic == 0x2478 ) {
		s->u.minix_sb.s_version = 0x0002 ;
		s->u.minix_sb.s_nzones = ms->s_zones;
		s->u.minix_sb.s_dirsize = 32;
		s->u.minix_sb.s_namelen = 30;
	} else {
		s->s_dev = 0;
		unlock_super(s);
		brelse(bh);
		if (!silent)
			printk("VFS: Can't find a minix or minix V2 filesystem on dev "
			       "%s.\n", kdevname(dev));
		do { } while (0) ;
		return ((void *) 0) ;
	}
	for (i=0;i < 8 ;i++)
		s->u.minix_sb.s_imap[i] = ((void *) 0) ;
	for (i=0;i < 64 ;i++)
		s->u.minix_sb.s_zmap[i] = ((void *) 0) ;
	if (s->u.minix_sb.s_zmap_blocks > 64 ) {
		s->s_dev = 0;
		unlock_super (s);
		brelse (bh);
		if (!silent)
			printk ("MINIX-fs: filesystem too big\n");
		do { } while (0) ;
		return ((void *) 0) ;
	}
	block=2;
	for (i=0 ; i < s->u.minix_sb.s_imap_blocks ; i++)
		if ((s->u.minix_sb.s_imap[i]=bread(dev,block,1024 )) != ((void *) 0) )
			block++;
		else
			break;
	for (i=0 ; i < s->u.minix_sb.s_zmap_blocks ; i++)
		if ((s->u.minix_sb.s_zmap[i]=bread(dev,block,1024 )) != ((void *) 0) )
			block++;
		else
			break;
	if (block != 2+s->u.minix_sb.s_imap_blocks+s->u.minix_sb.s_zmap_blocks) {
		for(i=0;i< 8 ;i++)
			brelse(s->u.minix_sb.s_imap[i]);
		for(i=0;i< 64 ;i++)
			brelse(s->u.minix_sb.s_zmap[i]);
		s->s_dev = 0;
		unlock_super(s);
		brelse(bh);
		printk("MINIX-fs: bad superblock or unable to read bitmaps\n");
		do { } while (0) ;
		return ((void *) 0) ;
	}
	set_bit(0,s->u.minix_sb.s_imap[0]->b_data);
	set_bit(0,s->u.minix_sb.s_zmap[0]->b_data);
	unlock_super(s);
	 
	s->s_dev = dev;
	s->s_op = &minix_sops;
	s->s_mounted = iget(s,1 );
	if (!s->s_mounted) {
		s->s_dev = 0;
		brelse(bh);
		if (!silent)
			printk("MINIX-fs: get root inode failed\n");
		do { } while (0) ;
		return ((void *) 0) ;
	}

	errmsg = minix_checkroot(s);
	if (errmsg) {
		if (!silent)
			printk("MINIX-fs: %s\n", errmsg);
		iput (s->s_mounted);
		s->s_dev = 0;
		brelse (bh);
		do { } while (0) ;
		return ((void *) 0) ;
	}

	if (!(s->s_flags & 1 )) {
		ms->s_state &= ~0x0001 ;
		mark_buffer_dirty(bh, 1);
		s->s_dirt = 1;
	}
	if (!(s->u.minix_sb.s_mount_state & 0x0001 ))
		printk ("MINIX-fs: mounting unchecked file system, "
			"running fsck is recommended.\n");
 	else if (s->u.minix_sb.s_mount_state & 0x0002 )
		printk ("MINIX-fs: mounting file system with errors, "
			"running fsck is recommended.\n");
	return s;
}

void minix_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct statfs tmp;

	tmp.f_type = sb->s_magic;
	tmp.f_bsize = sb->s_blocksize;
	tmp.f_blocks = (sb->u.minix_sb.s_nzones - sb->u.minix_sb.s_firstdatazone) << sb->u.minix_sb.s_log_zone_size;
	tmp.f_bfree = minix_count_free_blocks(sb);
	tmp.f_bavail = tmp.f_bfree;
	tmp.f_files = sb->u.minix_sb.s_ninodes;
	tmp.f_ffree = minix_count_free_inodes(sb);
	tmp.f_namelen = sb->u.minix_sb.s_namelen;
	memcpy_tofs(buf, &tmp, bufsiz);
}

 




static int V1_block_bmap(struct buffer_head * bh, int nr)
{
	int tmp;

	if (!bh)
		return 0;
	tmp = ((unsigned short *) bh->b_data)[nr];
	brelse(bh);
	return tmp;
}

static int V1_minix_bmap(struct inode * inode,int block)
{
	int i;

	if (block<0) {
		printk("minix_bmap: block<0");
		return 0;
	}
	if (block >= (inode->i_sb->u.minix_sb.s_max_size/ 1024 )) {
		printk("minix_bmap: block>big");
		return 0;
	}
	if (block < 7)
		return (((unsigned short *)( inode )->u.minix_i.u.i1_data)[( block )]) ;
	block -= 7;
	if (block < 512) {
		i = (((unsigned short *)( inode )->u.minix_i.u.i1_data)[( 7 )]) ;
		if (!i)
			return 0;
		return V1_block_bmap(bread(inode->i_dev,i,1024 ),block);
	}
	block -= 512;
	i = (((unsigned short *)( inode )->u.minix_i.u.i1_data)[( 8 )]) ;
	if (!i)
		return 0;
	i = V1_block_bmap(bread(inode->i_dev,i,1024 ),block>>9);
	if (!i)
		return 0;
	return V1_block_bmap(bread(inode->i_dev,i,1024 ),block & 511);
}

 



static int V2_block_bmap(struct buffer_head * bh, int nr)
{
	int tmp;

	if (!bh)
		return 0;
	tmp = ((unsigned long *) bh->b_data)[nr];
	brelse(bh);
	return tmp;
}

static int V2_minix_bmap(struct inode * inode,int block)
{
	int i;

	if (block<0) {
		printk("minix_bmap: block<0");
		return 0;
	}
	if (block >= (inode->i_sb->u.minix_sb.s_max_size/ 1024 )) {
		printk("minix_bmap: block>big");
		return 0;
	}
	if (block < 7)
		return (((unsigned long  *)( inode )->u.minix_i.u.i2_data)[( block )]) ;
	block -= 7;
	if (block < 256) {
		i = (((unsigned long  *)( inode )->u.minix_i.u.i2_data)[( 7 )]) ;
		if (!i)
			return 0;
		return V2_block_bmap(bread(inode->i_dev,i,1024 ),block);
	}
	block -= 256;
	if (block < 256*256) {
		i = (((unsigned long  *)( inode )->u.minix_i.u.i2_data)[( 8 )]) ;
		if (!i)
			return 0;
		i = V2_block_bmap(bread(inode->i_dev,i,1024 ),block >> 8);
		if (!i)
			return 0;
		return V2_block_bmap(bread(inode->i_dev,i,1024 ),block & 255);
	}
	block -= 256*256;
	i = (((unsigned long  *)( inode )->u.minix_i.u.i2_data)[( 9 )]) ;
	if (!i)
		return 0;
	i = V2_block_bmap(bread(inode->i_dev,i,1024 ),block >> 16);
	if (!i)
		return 0;
	i = V2_block_bmap(bread(inode->i_dev,i,1024 ),(block >> 8) & 255);
	if (!i)
		return 0;
	return V2_block_bmap(bread(inode->i_dev,i,1024 ),block & 255);
}

 


int minix_bmap(struct inode * inode,int block)
{
	if ( inode ->i_sb->u.minix_sb.s_version  == 0x0001 )
		return V1_minix_bmap(inode, block);
	else
		return V2_minix_bmap(inode, block);
}

 


static struct buffer_head * V1_inode_getblk(struct inode * inode, int nr, 
					    int create)
{
	int tmp;
	unsigned short *p;
	struct buffer_head * result;

	p = inode->u.minix_i.u.i1_data + nr;
repeat:
	tmp = *p;
	if (tmp) {
		result = getblk(inode->i_dev, tmp, 1024 );
		if (tmp == *p)
			return result;
		brelse(result);
		goto repeat;
	}
	if (!create)
		return ((void *) 0) ;
	tmp = minix_new_block(inode->i_sb);
	if (!tmp)
		return ((void *) 0) ;
	result = getblk(inode->i_dev, tmp, 1024 );
	if (*p) {
		minix_free_block(inode->i_sb,tmp);
		brelse(result);
		goto repeat;
	}
	*p = tmp;
	inode->i_ctime = (xtime.tv_sec) ;
	inode->i_dirt = 1;
	return result;
}

static struct buffer_head * V1_block_getblk(struct inode * inode, 
	struct buffer_head * bh, int nr, int create)
{
	int tmp;
	unsigned short *p;
	struct buffer_head * result;

	if (!bh)
		return ((void *) 0) ;
	if (!buffer_uptodate(bh)) {
		ll_rw_block(0 , 1, &bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			brelse(bh);
			return ((void *) 0) ;
		}
	}
	p = nr + (unsigned short *) bh->b_data;
repeat:
	tmp = *p;
	if (tmp) {
		result = getblk(bh->b_dev, tmp, 1024 );
		if (tmp == *p) {
			brelse(bh);
			return result;
		}
		brelse(result);
		goto repeat;
	}
	if (!create) {
		brelse(bh);
		return ((void *) 0) ;
	}
	tmp = minix_new_block(inode->i_sb);
	if (!tmp) {
		brelse(bh);
		return ((void *) 0) ;
	}
	result = getblk(bh->b_dev, tmp, 1024 );
	if (*p) {
		minix_free_block(inode->i_sb,tmp);
		brelse(result);
		goto repeat;
	}
	*p = tmp;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
	return result;
}

static struct buffer_head * V1_minix_getblk(struct inode * inode, int block, 
					    int create)
{
	struct buffer_head * bh;

	if (block<0) {
		printk("minix_getblk: block<0");
		return ((void *) 0) ;
	}
	if (block >= inode->i_sb->u.minix_sb.s_max_size/ 1024 ) {
		printk("minix_getblk: block>big");
		return ((void *) 0) ;
	}
	if (block < 7)
		return V1_inode_getblk(inode,block,create);
	block -= 7;
	if (block < 512) {
		bh = V1_inode_getblk(inode,7,create);
		return V1_block_getblk(inode, bh, block, create);
	}
	block -= 512;
	bh = V1_inode_getblk(inode,8,create);
	bh = V1_block_getblk(inode, bh, (block>>9) & 511, create);
	return V1_block_getblk(inode, bh, block & 511, create);
}

 


static struct buffer_head * V2_inode_getblk(struct inode * inode, int nr, 
					    int create)
{
	int tmp;
	unsigned long *p;
	struct buffer_head * result;

	p = (unsigned long *) inode->u.minix_i.u.i2_data + nr;
repeat:
	tmp = *p;
	if (tmp) {
		result = getblk(inode->i_dev, tmp, 1024 );
		if (tmp == *p)
			return result;
		brelse(result);
		goto repeat;
	}
	if (!create)
		return ((void *) 0) ;
	tmp = minix_new_block(inode->i_sb);
	if (!tmp)
		return ((void *) 0) ;
	result = getblk(inode->i_dev, tmp, 1024 );
	if (*p) {
		minix_free_block(inode->i_sb,tmp);
		brelse(result);
		goto repeat;
	}
	*p = tmp;
	inode->i_ctime = (xtime.tv_sec) ;
	inode->i_dirt = 1;
	return result;
}

static struct buffer_head * V2_block_getblk(struct inode * inode, 
	struct buffer_head * bh, int nr, int create)
{
	int tmp;
	unsigned long *p;
	struct buffer_head * result;

	if (!bh)
		return ((void *) 0) ;
	if (!buffer_uptodate(bh)) {
		ll_rw_block(0 , 1, &bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			brelse(bh);
			return ((void *) 0) ;
		}
	}
	p = nr + (unsigned long *) bh->b_data;
repeat:
	tmp = *p;
	if (tmp) {
		result = getblk(bh->b_dev, tmp, 1024 );
		if (tmp == *p) {
			brelse(bh);
			return result;
		}
		brelse(result);
		goto repeat;
	}
	if (!create) {
		brelse(bh);
		return ((void *) 0) ;
	}
	tmp = minix_new_block(inode->i_sb);
	if (!tmp) {
		brelse(bh);
		return ((void *) 0) ;
	}
	result = getblk(bh->b_dev, tmp, 1024 );
	if (*p) {
		minix_free_block(inode->i_sb,tmp);
		brelse(result);
		goto repeat;
	}
	*p = tmp;
	mark_buffer_dirty(bh, 1);
	brelse(bh);
	return result;
}

static struct buffer_head * V2_minix_getblk(struct inode * inode, int block, 
					    int create)
{
	struct buffer_head * bh;

	if (block<0) {
		printk("minix_getblk: block<0");
		return ((void *) 0) ;
	}
	if (block >= inode->i_sb->u.minix_sb.s_max_size/ 1024 ) {
		printk("minix_getblk: block>big");
		return ((void *) 0) ;
	}
	if (block < 7)
		return V2_inode_getblk(inode,block,create);
	block -= 7;
	if (block < 256) {
		bh = V2_inode_getblk(inode,7,create);
		return V2_block_getblk(inode, bh, block, create);
	}
	block -= 256;
	if (block < 256*256) {
		bh = V2_inode_getblk(inode,8,create);
		bh = V2_block_getblk(inode, bh, (block>>8) & 255, create);
		return V2_block_getblk(inode, bh, block & 255, create);
	}
	block -= 256*256;
	bh = V2_inode_getblk(inode,9,create);
	bh = V2_block_getblk(inode, bh, (block >> 16) & 255, create);
	bh = V2_block_getblk(inode, bh, (block >> 8) & 255, create);
	return V2_block_getblk(inode, bh, block & 255, create);
}

 


struct buffer_head * minix_getblk(struct inode * inode, int block, int create)
{
	if ( inode ->i_sb->u.minix_sb.s_version  == 0x0001 )
		return V1_minix_getblk(inode,block,create);
	else
		return V2_minix_getblk(inode,block,create);
}

struct buffer_head * minix_bread(struct inode * inode, int block, int create)
{
	struct buffer_head * bh;

	bh = minix_getblk(inode,block,create);
	if (!bh || buffer_uptodate(bh))
		return bh;
	ll_rw_block(0 , 1, &bh);
	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		return bh;
	brelse(bh);
	return ((void *) 0) ;
}

 


static void V1_minix_read_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix_inode * raw_inode;
	int block, ino;

	ino = inode->i_ino;
	inode->i_op = ((void *) 0) ;
	inode->i_mode = 0;
	if (!ino || ino >= inode->i_sb->u.minix_sb.s_ninodes) {
		printk("Bad inode number on dev %s"
		       ": %d is out of range\n",
			kdevname(inode->i_dev), ino);
		return;
	}
	block = 2 + inode->i_sb->u.minix_sb.s_imap_blocks +
		    inode->i_sb->u.minix_sb.s_zmap_blocks +
		    (ino-1)/ ((1024 )/(sizeof (struct minix_inode))) ;
	if (!(bh=bread(inode->i_dev,block, 1024 ))) {
		printk("Major problem: unable to read inode from dev "
		       "%s\n", kdevname(inode->i_dev));
		return;
	}
	raw_inode = ((struct minix_inode *) bh->b_data) +
		    (ino-1)% ((1024 )/(sizeof (struct minix_inode))) ;
	inode->i_mode = raw_inode->i_mode;
	inode->i_uid = raw_inode->i_uid;
	inode->i_gid = raw_inode->i_gid;
	inode->i_nlink = raw_inode->i_nlinks;
	inode->i_size = raw_inode->i_size;
	inode->i_mtime = inode->i_atime = inode->i_ctime = raw_inode->i_time;
	inode->i_blocks = inode->i_blksize = 0;
	if (((( inode->i_mode ) & 00170000 ) == 0020000 )  || ((( inode->i_mode ) & 00170000 ) == 0060000 ) )
		inode->i_rdev = to_kdev_t(raw_inode->i_zone[0]);
	else for (block = 0; block < 9; block++)
		inode->u.minix_i.u.i1_data[block] = raw_inode->i_zone[block];
	brelse(bh);
	if (((( inode->i_mode ) & 00170000 ) == 0100000 ) )
		inode->i_op = &minix_file_inode_operations;
	else if (((( inode->i_mode ) & 00170000 ) == 0040000 ) )
		inode->i_op = &minix_dir_inode_operations;
	else if (((( inode->i_mode ) & 00170000 ) == 0120000 ) )
		inode->i_op = &minix_symlink_inode_operations;
	else if (((( inode->i_mode ) & 00170000 ) == 0020000 ) )
		inode->i_op = &chrdev_inode_operations;
	else if (((( inode->i_mode ) & 00170000 ) == 0060000 ) )
		inode->i_op = &blkdev_inode_operations;
	else if (((( inode->i_mode ) & 00170000 ) == 0010000 ) )
		init_fifo(inode);
}

 


static void V2_minix_read_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix2_inode * raw_inode;
	int block, ino;

	ino = inode->i_ino;
	inode->i_op = ((void *) 0) ;
	inode->i_mode = 0;
	if (!ino || ino >= inode->i_sb->u.minix_sb.s_ninodes) {
		printk("Bad inode number on dev %s"
		       ": %d is out of range\n",
			kdevname(inode->i_dev), ino);
		return;
	}
	block = 2 + inode->i_sb->u.minix_sb.s_imap_blocks +
		    inode->i_sb->u.minix_sb.s_zmap_blocks +
		    (ino-1)/ ((1024 )/(sizeof (struct minix2_inode))) ;
	if (!(bh=bread(inode->i_dev,block, 1024 ))) {
		printk("Major problem: unable to read inode from dev "
		       "%s\n", kdevname(inode->i_dev));
		return;
	}
	raw_inode = ((struct minix2_inode *) bh->b_data) +
		    (ino-1)% ((1024 )/(sizeof (struct minix2_inode))) ;
	inode->i_mode = raw_inode->i_mode;
	inode->i_uid = raw_inode->i_uid;
	inode->i_gid = raw_inode->i_gid;
	inode->i_nlink = raw_inode->i_nlinks;
	inode->i_size = raw_inode->i_size;
	inode->i_mtime = raw_inode->i_mtime;
	inode->i_atime = raw_inode->i_atime;
	inode->i_ctime = raw_inode->i_ctime;
	inode->i_blocks = inode->i_blksize = 0;
	if (((( inode->i_mode ) & 00170000 ) == 0020000 )  || ((( inode->i_mode ) & 00170000 ) == 0060000 ) )
		inode->i_rdev = to_kdev_t(raw_inode->i_zone[0]);
	else for (block = 0; block < 10; block++)
		inode->u.minix_i.u.i2_data[block] = raw_inode->i_zone[block];
	brelse(bh);
	if (((( inode->i_mode ) & 00170000 ) == 0100000 ) )
		inode->i_op = &minix_file_inode_operations;
	else if (((( inode->i_mode ) & 00170000 ) == 0040000 ) )
		inode->i_op = &minix_dir_inode_operations;
	else if (((( inode->i_mode ) & 00170000 ) == 0120000 ) )
		inode->i_op = &minix_symlink_inode_operations;
	else if (((( inode->i_mode ) & 00170000 ) == 0020000 ) )
		inode->i_op = &chrdev_inode_operations;
	else if (((( inode->i_mode ) & 00170000 ) == 0060000 ) )
		inode->i_op = &blkdev_inode_operations;
	else if (((( inode->i_mode ) & 00170000 ) == 0010000 ) )
		init_fifo(inode);
}

 


void minix_read_inode(struct inode * inode)
{
	if ( inode ->i_sb->u.minix_sb.s_version  == 0x0001 )
		V1_minix_read_inode(inode);
	else
		V2_minix_read_inode(inode);
}

 


static struct buffer_head * V1_minix_update_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix_inode * raw_inode;
	int ino, block;

	ino = inode->i_ino;
	if (!ino || ino >= inode->i_sb->u.minix_sb.s_ninodes) {
		printk("Bad inode number on dev %s"
		       ": %d is out of range\n",
			kdevname(inode->i_dev), ino);
		inode->i_dirt = 0;
		return 0;
	}
	block = 2 + inode->i_sb->u.minix_sb.s_imap_blocks + inode->i_sb->u.minix_sb.s_zmap_blocks +
		(ino-1)/ ((1024 )/(sizeof (struct minix_inode))) ;
	if (!(bh=bread(inode->i_dev, block, 1024 ))) {
		printk("unable to read i-node block\n");
		inode->i_dirt = 0;
		return 0;
	}
	raw_inode = ((struct minix_inode *)bh->b_data) +
		(ino-1)% ((1024 )/(sizeof (struct minix_inode))) ;
	raw_inode->i_mode = inode->i_mode;
	raw_inode->i_uid = inode->i_uid;
	raw_inode->i_gid = inode->i_gid;
	raw_inode->i_nlinks = inode->i_nlink;
	raw_inode->i_size = inode->i_size;
	raw_inode->i_time = inode->i_mtime;
	if (((( inode->i_mode ) & 00170000 ) == 0020000 )  || ((( inode->i_mode ) & 00170000 ) == 0060000 ) )
		raw_inode->i_zone[0] = kdev_t_to_nr(inode->i_rdev);
	else for (block = 0; block < 9; block++)
		raw_inode->i_zone[block] = inode->u.minix_i.u.i1_data[block];
	inode->i_dirt=0;
	mark_buffer_dirty(bh, 1);
	return bh;
}

 


static struct buffer_head * V2_minix_update_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct minix2_inode * raw_inode;
	int ino, block;

	ino = inode->i_ino;
	if (!ino || ino >= inode->i_sb->u.minix_sb.s_ninodes) {
		printk("Bad inode number on dev %s"
		       ": %d is out of range\n",
			kdevname(inode->i_dev), ino);
		inode->i_dirt = 0;
		return 0;
	}
	block = 2 + inode->i_sb->u.minix_sb.s_imap_blocks + inode->i_sb->u.minix_sb.s_zmap_blocks +
		(ino-1)/ ((1024 )/(sizeof (struct minix2_inode))) ;
	if (!(bh=bread(inode->i_dev, block, 1024 ))) {
		printk("unable to read i-node block\n");
		inode->i_dirt = 0;
		return 0;
	}
	raw_inode = ((struct minix2_inode *)bh->b_data) +
		(ino-1)% ((1024 )/(sizeof (struct minix2_inode))) ;
	raw_inode->i_mode = inode->i_mode;
	raw_inode->i_uid = inode->i_uid;
	raw_inode->i_gid = inode->i_gid;
	raw_inode->i_nlinks = inode->i_nlink;
	raw_inode->i_size = inode->i_size;
	raw_inode->i_mtime = inode->i_mtime;
	raw_inode->i_atime = inode->i_atime;
	raw_inode->i_ctime = inode->i_ctime;
	if (((( inode->i_mode ) & 00170000 ) == 0020000 )  || ((( inode->i_mode ) & 00170000 ) == 0060000 ) )
		raw_inode->i_zone[0] = kdev_t_to_nr(inode->i_rdev);
	else for (block = 0; block < 10; block++)
		raw_inode->i_zone[block] = inode->u.minix_i.u.i2_data[block];
	inode->i_dirt=0;
	mark_buffer_dirty(bh, 1);
	return bh;
}

struct buffer_head *minix_update_inode(struct inode *inode)
{
	if ( inode ->i_sb->u.minix_sb.s_version  == 0x0001 )
		return V1_minix_update_inode(inode);
	else
		return V2_minix_update_inode(inode);
}

void minix_write_inode(struct inode * inode)
{
	struct buffer_head *bh;

	bh = minix_update_inode(inode);
	brelse(bh);
}

int minix_sync_inode(struct inode * inode)
{
	int err = 0;
	struct buffer_head *bh;

	bh = minix_update_inode(inode);
	if (bh && buffer_dirty(bh))
	{
		ll_rw_block(1 , 1, &bh);
		wait_on_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh))
		{
			printk ("IO error syncing minix inode ["
				"%s:%08lx]\n",
				kdevname(inode->i_dev), inode->i_ino);
			err = -1;
		}
	}
	else if (!bh)
		err = -1;
	brelse (bh);
	return err;
}

static struct file_system_type minix_fs_type = {
	minix_read_super, "minix", 1, ((void *) 0) 
};

int init_minix_fs(void)
{
        return register_filesystem(&minix_fs_type);
}

# 967 "inode.c"

