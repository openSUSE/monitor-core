/*
 *  Rewrite of AIX metrics using libperfstat API
 *
 *  Libperfstat can deal with a 32-bit and a 64-bit Kernel and does not require root authority.
 *
 *  The code is tested with AIX 5.2 (32Bit- and 64Bit-Kernel), but 5.1 and 5.3 shoud be OK too
 *
 *  by Andreas Schoenfeld, TU Darmstadt, Germany (4/2005) 
 *  E-Mail: Schoenfeld@hrz.tu-darmstadt.de
 *
 *  Its based on the 
 *  First stab at support for metrics in AIX
 *  by Preston Smith <psmith@physics.purdue.edu>
 *  Wed Feb 27 14:55:33 EST 2002
 *
 *  AIX V5 support, bugfixes added by Davide Tacchella <tack@cscs.ch>
 *  May 10, 2002
 *
 *  you may still find some code (like "int bos_level(..)"  ) and the basic structure of this version. 
 *
 *  Some code fragments of the network statistics are "borowed" from  
 *  the Solaris Metrics   
 *
 */

#include "interface.h"
#include <stdlib.h>
#include <utmp.h>
#include <stdio.h>
#include <procinfo.h>
#include <strings.h>
#include <signal.h>
#include <odmi.h>
#include <cf.h>
#include <sys/utsname.h>

#include <libperfstat.h>

#include "libmetrics.h"




struct Class *My_CLASS;

struct product {
        char filler[12];
        char lpp_name[145];      /* offset: 0xc ( 12) */
        char comp_id[20];        /* offset: 0x9d ( 157) */
        short update;            /* offset: 0xb2 ( 178) */
        long cp_flag;            /* offset: 0xb4 ( 180) */
        char fesn[10];           /* offset: 0xb8 ( 184) */
        char *name;              /*[42] offset: 0xc4 ( 196) */
        short state;             /* offset: 0xc8 ( 200) */
        short ver;               /* offset: 0xca ( 202) */
        short rel;               /* offset: 0xcc ( 204) */
        short mod;               /* offset: 0xce ( 206) */
        short fix;               /* offset: 0xd0 ( 208) */
        char ptf[10];            /* offset: 0xd2 ( 210) */
        short media;             /* offset: 0xdc ( 220) */
        char sceded_by[10];      /* offset: 0xde ( 222) */
        char *fixinfo;           /* [1024] offset: 0xe8 ( 232) */
        char *prereq;            /* [1024] offset: 0xec ( 236) */
        char *description;       /* [1024] offset: 0xf0 ( 240) */
        char *supersedes;        /* [512] offset: 0xf4 ( 244) */
};



#define MAX_CPUS  64

#define INFO_TIMEOUT   10
#define CPU_INFO_TIMEOUT INFO_TIMEOUT

#define MEM_PAGESIZE 4096/1024


struct cpu_info {
  time_t timestamp;  
  u_longlong_t total_ticks;
  u_longlong_t user;        /*  raw total number of clock ticks spent in user mode */
  u_longlong_t sys;         /* raw total number of clock ticks spent in system mode */
  u_longlong_t idle;        /* raw total number of clock ticks spent idle */
  u_longlong_t wait;        /* raw total number of clock ticks spent waiting for I/O */
};

struct net_stat{
  double ipackets;
  double opackets;
  double ibytes;
  double obytes;
} cur_net_stat;






int ci_flag=0;
int ni_flag=0;

perfstat_cpu_total_t cpu_total_buffer;
perfstat_memory_total_t minfo;
perfstat_disk_total_t dinfo;
perfstat_netinterface_total_t ninfo[2],*last_ninfo, *cur_ninfo ;


struct cpu_info cpu_info[2], 
  *last_cpu_info,
  *cur_cpu_info;


  

int aixver, aixrel, aixlev, aixfix;
struct utsname unames;


/* Prototypes
 */
void  update_ifdata(void);
void get_cpuinfo(void);
int bos_level(int *aix_version, int *aix_release, int *aix_level, int *aix_fix);





/*
 * This function is called only once by the gmond.  Use to 
 * initialize data structures, etc or just return SYNAPSE_SUCCESS;
 */
g_val_t
metric_init(void)
{
   g_val_t val;


   last_cpu_info = &cpu_info[ci_flag];
   ci_flag^=1;
   cur_cpu_info  = &cpu_info[ci_flag];
   cur_cpu_info->total_ticks = 0;
   
   update_ifdata();
   uname(&unames);
   
   get_cpuinfo();
   sleep(CPU_INFO_TIMEOUT+1);
   get_cpuinfo();

   perfstat_memory_total(NULL, &minfo, sizeof(perfstat_memory_total_t), 1);  
   perfstat_disk_total(NULL, &dinfo, sizeof(perfstat_disk_total_t), 1);

   update_ifdata();

   bos_level(&aixver, &aixrel, &aixlev, &aixfix);

   val.int32 = SYNAPSE_SUCCESS;
   return val;
}



g_val_t
cpu_speed_func ( void )
{
   g_val_t val;

   perfstat_cpu_total(NULL,  &cpu_total_buffer, sizeof(perfstat_cpu_total_t), 1);
   val.uint32 = cpu_total_buffer.processorHZ/1000000;

   return val;
}


g_val_t
boottime_func ( void )
{
   g_val_t val;
   int boottime = 0;
   struct utmp buf;
   FILE *utmp = fopen(UTMP_FILE, "r");

   if (utmp == NULL) {
     /* Can't open utmp, use current time as boottime */
     boottime = time(NULL);
   } else {
     while (fread((char *) &buf, sizeof(buf), 1, utmp) == 1) {
        if (buf.ut_type == BOOT_TIME) {
            boottime = buf.ut_time;
            break;
        }
     }
   }
   val.uint32 = boottime;
   return val;
}

g_val_t
sys_clock_func ( void )
{
   g_val_t val;

   val.uint32 = time(NULL);
   return val;
}



g_val_t
machine_type_func ( void )
{
   g_val_t val;


   strncpy( val.str,cpu_total_buffer.description , MAX_G_STRING_SIZE );
   return val;
}


g_val_t
os_name_func ( void )
{
   g_val_t val;
   strncpy( val.str, unames.sysname, MAX_G_STRING_SIZE );
   return val;
}        


g_val_t
os_release_func ( void )
{
   g_val_t val;
   char oslevel[MAX_G_STRING_SIZE];

   sprintf(oslevel, "%d.%d.%d.%d", aixver, aixrel, aixlev, aixfix);
   strncpy( val.str, oslevel, MAX_G_STRING_SIZE );

   return val;
}        


/* AIX  defines
   CPU_IDLE, CPU_USER, CPU_SYS(CPU_KERNEL), CPU_WAIT
   so no metrics for cpu_nice, or cpu_aidle
*/


#define CALC_CPUINFO(type) ((100.0*(cur_cpu_info->type - last_cpu_info->type))/(1.0*(cur_cpu_info->total_ticks - last_cpu_info->total_ticks)))

g_val_t
cpu_user_func ( void )
{
   g_val_t val;
   
   
   get_cpuinfo();
   
   val.f = CALC_CPUINFO(user);

   if(val.f < 0) val.f = 0.0;
   return val;
}


/*
** AIX dosen't not that 
*/

g_val_t
cpu_nice_func ( void )
{
   g_val_t val;
   val.f = 0;
   return val;
}

g_val_t 
cpu_system_func ( void )
{
   g_val_t val;

   get_cpuinfo();
   val.f = CALC_CPUINFO(sys) ;
   if(val.f < 0) val.f = 0.0;
   return val;
}
g_val_t 

cpu_wio_func ( void )
{
   g_val_t val;
   
   get_cpuinfo();
   val.f = CALC_CPUINFO(wait);


   if(val.f < 0) val.f = 0.0;
   return val;
}

g_val_t 
cpu_idle_func ( void )
{
   g_val_t val;


   get_cpuinfo();
   val.f = CALC_CPUINFO(idle);


   if(val.f < 0) val.f = 0.0;
   return val;
}

/*
** AIX dosen't not that 
*/
g_val_t 
cpu_aidle_func ( void )
{
   g_val_t val;
   val.f = 0.0;
   return val;
}

/*
** Don't know what it is 
*/
g_val_t 
cpu_intr_func ( void )
{
   g_val_t val;
   val.f = 0.0;
   return val;
}

/* Don't know what it is 
** FIXME -- 
*/
g_val_t 
cpu_sintr_func ( void )
{
   g_val_t val;
   val.f = 0.0;
   return val;
}


g_val_t 
bytes_in_func ( void )
{
   g_val_t val;
   update_ifdata();
   val.f = cur_net_stat.ibytes;
   return val;
}


g_val_t 
bytes_out_func ( void )
{
   g_val_t val;

   update_ifdata();
   val.f = cur_net_stat.obytes;
   
   return val;
}


g_val_t 
pkts_in_func ( void )
{
   g_val_t val;

   update_ifdata();
   val.f = cur_net_stat.ipackets;

   return val;
}


g_val_t 
pkts_out_func ( void )
{
   g_val_t val;


   update_ifdata();
   val.f = cur_net_stat.opackets;

   return val;
}


g_val_t 
disk_free_func ( void )
{
   g_val_t val;
   
   perfstat_disk_total(NULL, &dinfo, sizeof(perfstat_disk_total_t), 1);
   val.d = dinfo.free;
   return val;
}


g_val_t 
disk_total_func ( void )
{
   g_val_t val;
   perfstat_disk_total(NULL, &dinfo, sizeof(perfstat_disk_total_t), 1);
   val.d =dinfo.size ;
   return val;
}

/* most used Partition dose not make sense to me 
** FIXME
*/
g_val_t 
part_max_used_func ( void )
{
   g_val_t val;
   val.f = 0.0;
   return val;
}



g_val_t
load_one_func ( void )
{
   g_val_t val;
   
   perfstat_cpu_total(NULL,  &cpu_total_buffer, sizeof(perfstat_cpu_total_t), 1);
   val.f =(1.0*cpu_total_buffer.loadavg[0])/(1<<SBITS) ;
   return val;
}

g_val_t
load_five_func ( void )
{  
   g_val_t val;

   perfstat_cpu_total(NULL,  &cpu_total_buffer, sizeof(perfstat_cpu_total_t), 1);
   val.f =(1.0*cpu_total_buffer.loadavg[1])/(1<<SBITS) ;
   return val;
}

g_val_t
load_fifteen_func ( void )
{
   g_val_t val;

   perfstat_cpu_total(NULL,  &cpu_total_buffer, sizeof(perfstat_cpu_total_t), 1);
   val.f =(1.0*cpu_total_buffer.loadavg[2])/(1<<SBITS)*1.0 ;

   return val;
}

g_val_t
cpu_num_func ( void )
{
   g_val_t val;

   val.uint16 =  cpu_total_buffer.ncpus;
   return val;
}



g_val_t
proc_total_func ( void )
{
  g_val_t foo;
 
  foo.uint32 = cpu_total_buffer.ncpus;
 
  return foo;
}


g_val_t
proc_run_func( void )
{
  g_val_t val;

  perfstat_cpu_total(NULL,  &cpu_total_buffer, sizeof(perfstat_cpu_total_t), 1);
  val.uint32 = cpu_total_buffer.ncpus_cfg;

 
  return val;
}



g_val_t
mem_total_func ( void )
{
   g_val_t val;
  
   perfstat_memory_total(NULL, &minfo, sizeof(perfstat_memory_total_t), 1);

   val.uint32 = minfo.real_total*MEM_PAGESIZE;
   
   return val;
}

g_val_t
mem_free_func ( void )
{
   g_val_t val;
  
   perfstat_memory_total(NULL, &minfo, sizeof(perfstat_memory_total_t), 1);
   
   val.uint32 = minfo.real_free*MEM_PAGESIZE; 
   return val;
}

g_val_t
mem_shared_func ( void )
{
   g_val_t val;
   val.uint32 = 0;

   return val;
}

g_val_t
mem_buffers_func ( void )
{
   g_val_t val;
   val.uint32 = 0;
   return val;
}

g_val_t
mem_cached_func ( void )
{
   g_val_t val;
   val.uint32 = 0;
   return val;
}

g_val_t
swap_total_func ( void )
{
   g_val_t val;

   perfstat_memory_total(NULL, &minfo, sizeof(perfstat_memory_total_t), 1);
   
   val.uint32 =minfo.pgsp_total ;
   return val;
   
}

g_val_t
swap_free_func ( void )
{
   g_val_t val;
   perfstat_memory_total(NULL, &minfo, sizeof(perfstat_memory_total_t), 1);
   
   val.uint32 =minfo.pgsp_free;

   return val;
}


g_val_t
mtu_func ( void )
{
   /* We want to find the minimum MTU (Max packet size) over all UP interfaces.
*/
   unsigned int min=0;
   g_val_t val;
   val.uint32 = get_min_mtu();
   /* A val of 0 means there are no UP interfaces. Shouldn't happen. */
   return val;
}








void get_cpuinfo() 
{
  u_longlong_t cpu_total;
  time_t new_time;


  new_time = time(NULL);

  if (new_time - CPU_INFO_TIMEOUT > cur_cpu_info->timestamp ) 
    {

      perfstat_cpu_total(NULL,  &cpu_total_buffer, sizeof(perfstat_cpu_total_t), 1);


      cpu_total = cpu_total_buffer.user +  cpu_total_buffer.sys  
	+  cpu_total_buffer.idle +  cpu_total_buffer.wait;

  
      last_cpu_info=&cpu_info[ci_flag];
      ci_flag^=1; 
      cur_cpu_info=&cpu_info[ci_flag];

      cur_cpu_info->timestamp   = new_time;
      cur_cpu_info->total_ticks = cpu_total;
      cur_cpu_info->user        = cpu_total_buffer.user;
      cur_cpu_info->sys         = cpu_total_buffer.sys;
      cur_cpu_info->idle        = cpu_total_buffer.idle;
      cur_cpu_info->wait        = cpu_total_buffer.wait;
    }
} /*      get_cpuinfo  */
  


/* int bos_level(int *aix_version, int *aix_release, int *aix_level, int *aix_fix)
 *  is copied form 
 *
 *  First stab at support for metrics in AIX
 *  by Preston Smith <psmith@physics.purdue.edu>
 *  Wed Feb 27 14:55:33 EST 2002
 *
 *  AIX V5 support, bugfixes added by Davide Tacchella <tack@cscs.ch>
 *  May 10, 2002
 *
 */

int bos_level(int *aix_version, int *aix_release, int *aix_level, int *aix_fix)
{
    struct Class *my_cl;   /* customized devices class ptr */
    struct product  productobj;     /* customized device object storage */
    int rc, getit, found = 0;
    char *path;

    /*
     * start up odm
     */
    if (odm_initialize() == -1)
        return E_ODMINIT; 

    /*
     * Make sure we take the right database
     */
    if ((path = odm_set_path("/usr/lib/objrepos")) == (char *) -1)
        return odmerrno;
 
    /*
     * Mount the lpp class
     */
    if ((My_CLASS = odm_mount_class("product")) == (CLASS_SYMBOL) -1)
        return odmerrno;

    /*
     * open customized devices object class
     */
    if ((int)(my_cl = odm_open_class(My_CLASS)) == -1)
        return E_ODMOPEN;

    /*
     * Loop trough all entries for the lpp name, ASSUMING the last
     * one denotes the up to date number!!!
     */
    /*
     * AIX > 4.2 uses bos.mp or bos.up
     */
    getit = ODM_FIRST;
    while ((rc = (int)odm_get_obj(my_cl, "name like bos.?p",
                                  &productobj, getit)) != 0) {
        getit = ODM_NEXT;
        if (rc == -1) {
            /* ODM failure */
            break;
        }
        else {
            *aix_version = productobj.ver;
            *aix_release = productobj.rel;
            *aix_level   = productobj.mod;
            *aix_fix     = productobj.fix;
            found++;
        }
    }
    /*
     * AIX < 4.2 uses bos.mp or bos.up
     */
    if (!found) {
        getit = ODM_FIRST;
        while ((rc = (int)odm_get_obj(my_cl, "name like bos.rte.?p",
                                      &productobj, getit)) != 0) {
            getit = ODM_NEXT;
            if (rc == -1) {
                /* ODM failure */
                break;
            }
            else {
                *aix_version = productobj.ver;
                *aix_release = productobj.rel;
                *aix_level   = productobj.mod;
                *aix_fix     = productobj.fix;
                found++;
            }
        }
    }



    /*
     * close lpp object class
     */
    odm_close_class(my_cl);

    odm_terminate();

    free(path);
    return (found ? 0 : -1);

} /* bos_level */



#define CALC_NETSTAT(type) (double) ( (cur_ninfo->type - last_ninfo->type)/timediff)

void 
update_ifdata(void){

   static int init_done = 0;
   static struct timeval lasttime={0,0};
   struct timeval thistime;
   double timediff;
   

   /*
   ** Compute time between calls
   */
   gettimeofday (&thistime, NULL);
   if (lasttime.tv_sec)
     timediff = ((double) thistime.tv_sec * 1.0e6 +
                 (double) thistime.tv_usec -
                 (double) lasttime.tv_sec * 1.0e6 -
                 (double) lasttime.tv_usec) / 1.0e6;
   else
     timediff = 1.0;

   /*
   ** Do nothing if we are called to soon after the last call
   */
   if (init_done && (timediff < INFO_TIMEOUT)) return;
   
   lasttime = thistime;

   last_ninfo = &ninfo[ni_flag];

   ni_flag^=1;

   cur_ninfo = &ninfo[ni_flag];

   perfstat_netinterface_total(NULL, cur_ninfo, sizeof(perfstat_netinterface_total_t), 1);

   if (init_done) {
      cur_net_stat.ipackets = CALC_NETSTAT(ipackets);
      cur_net_stat.opackets = CALC_NETSTAT(opackets);
      cur_net_stat.ibytes   = CALC_NETSTAT(ibytes);
      cur_net_stat.obytes   = CALC_NETSTAT(obytes);
   }
   else
     {
       init_done = 1;
       cur_net_stat.ipackets = 0;
       cur_net_stat.opackets = 0;
       cur_net_stat.ibytes   = 0;
       cur_net_stat.obytes   = 0;
     }

}  /* update_ifdata */
