#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#define NV_FS_TEST_HOST
#include "../kernel/memory.c"
#include "../kernel/fs.c"
#include "../kernel/account.c"
struct task tasks[NV_TASK_MAX];
volatile u64 ticks;
bool test_mode;
static bool random_available=true;
static int sync_error,session_ends;
static bool lock_during_sync;
static u32 network_releases,released_uid;
static u32 released_pids;
static u32 console_locks,window_locks,lock_manager,lock_uid;
void console_session_lock(u32 manager) {
    ++console_locks;lock_manager=manager;lock_uid=session_uid;
}
void window_session_lock(void) { ++window_locks;assert(window_locks==console_locks); }
static void *io_buffer;
void panic(const char *s) { fprintf(stderr,"PANIC: %s\n",s);abort(); }
void console_write(const char *s,usize n) { (void)s;(void)n; }
int console_getc(void) { return -NV_EAGAIN; }
int task_info(u32 i,struct nv_taskinfo *out) { (void)i;(void)out;return 0; }
bool task_cwd_in_use(int node) { (void)node;return false; }
bool user_range(pte_t *pd, uptr p, usize n,bool write) {
    (void)pd;(void)write;return p>=(u32)(uptr)io_buffer && (u64)p+n<=(uptr)io_buffer+PAGE;
}
bool disk_ready(void) { return true; }
int store_sync(void) {
    if (lock_during_sync) {
        lock_during_sync=false;
        ticks=last_activity+idle_timeout;account_tick();
        assert(locked && !account_job.active);
    }
    return sync_error;
}
int store_write_error(u32 v) { (void)v;return 0; }
int store_read_bytes(u32 v,int slot,u32 off,void *dst,u32 len) {
    (void)v;(void)slot;(void)off;(void)dst;(void)len;return -NV_EIO;
}
int disk_volume_read(u32 v,u64 lba,void *p) { (void)v;(void)lba;(void)p;return -NV_EIO; }
int disk_volume_write(u32 v,u64 lba,const void *p) { (void)v;(void)lba;(void)p;return -NV_EIO; }
bool cpu_random(void *out,u32 n) {
    if (!random_available) return false;
    static u8 sequence;u8 *p=out;while (n--) *p++=++sequence;return true;
}
void task_end_session(u32 except) { assert(except==1 || except==2);++session_ends; }
void net_task_release(u32 pid) {
    assert((pid==manager_pid || pid==1) && current->pid==manager_pid);
    released_pids|=1u<<pid;released_uid=current->uid;++network_releases;
}
static int call(u32 op,const void *data) {
    if (data) memcpy(io_buffer,data,sizeof(struct nv_account_request));
    return account_ioctl(op,data?(u32)(uptr)io_buffer:0);
}
static int run(u32 op,struct nv_account_request *request) {
    int r=call(op,request);if (r<0) return r;
    u32 last=0;
    do {
        r=account_ioctl(NV_ACCOUNT_POLL,(u32)(uptr)io_buffer);
        struct nv_account_progress *p=io_buffer;
        assert(r>=0 && p->completed>=last && p->completed<=p->total);last=p->completed;
        if (r) { assert(p->completed==p->total);return p->result; }
    } while (!r);
    return -999;
}
static struct nv_account_request credentials(const char *name,const char *password) {
    struct nv_account_request r={0};strlcpy(r.name,name,32);strlcpy(r.display_name,name,64);
    strlcpy(r.password,password,sizeof(r.password));return r;
}
static void actor(u32 slot,u32 uid) {
    current=&tasks[slot];current->uid=uid;current->cwd=home_node;
    for (u32 i=0;i<NV_OPEN_MAX;++i) current->fd[i].node=-1;
}
int main(void) {
    heap=aligned_alloc(PAGE,HEAP_SIZE);assert(heap);head=(struct block *)heap;
    *head=(struct block){.magic=BLOCK_MAGIC,.size=HEAP_SIZE-sizeof(*head),.free=1};fs_init();
    io_buffer=mmap(NULL,PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    assert(io_buffer!=MAP_FAILED && (uptr)io_buffer+PAGE<=0xffffffffu);
    tasks[0].pid=1;tasks[0].state=NV_READY;actor(0,NV_UID_NONE);account_init();
    /* PID 1 alone is not a login-manager capability. Program nodes come
     * from the immutable archive, not a pathname supplied by an app. */
    assert(call(NV_ACCOUNT_CLAIM,NULL)==-NV_EACCESS);
    current->program_node=make_node(fs_lookup(0,"/apps"),"loom",NV_FILE,true);
    assert(current->program_node>=0);
    assert(!call(NV_ACCOUNT_CLAIM,NULL));
    /* An idle account service must not invent a delay at long uptimes. */
    struct nv_account_info info;
    ticks=0x80000001u;get_info(&info);assert(!info.cooldown_ticks);
    ticks=0;
    struct nv_account_request admin=credentials("alice","a long local password");
    random_available=false;assert(run(NV_ACCOUNT_SETUP,&admin)==-NV_ENODEV);random_available=true;
    admin.password[0]=0;assert(run(NV_ACCOUNT_SETUP,&admin)==-NV_EINVAL);
    admin=credentials("alice","a long local password");assert(!run(NV_ACCOUNT_SETUP,&admin));
    assert(database.count==1 && session_uid==1000 && current->uid==1000 && database.records[0].role==1);
    assert(account_admin_allowed(current));
    struct task inherited_pid={.pid=1,.uid=NV_UID_NONE};
    assert(!account_admin_allowed(&inherited_pid));
    struct nv_account_policy *policy=io_buffer;
    assert(!account_ioctl(NV_ACCOUNT_POLICY_GET,(uptr)policy) && policy->idle_timeout_ticks==NV_AUTH_IDLE_DEFAULT);
    policy->idle_timeout_ticks=0;assert(account_ioctl(NV_ACCOUNT_POLICY_SET,(uptr)policy)==-NV_EINVAL);
    policy->idle_timeout_ticks=NV_AUTH_IDLE_MAX+1;assert(account_ioctl(NV_ACCOUNT_POLICY_SET,(uptr)policy)==-NV_EINVAL);
    policy->idle_timeout_ticks=NV_AUTH_IDLE_MIN;assert(!account_ioctl(NV_ACCOUNT_POLICY_SET,(uptr)policy));
    ticks=NV_AUTH_IDLE_MIN-1;account_tick();assert(!locked);
    account_input_activity();ticks+=NV_AUTH_IDLE_MIN-1;account_tick();assert(!locked);
    ++ticks;account_tick();assert(locked && !account_admin_allowed(current) && !account_session_allowed(current));
    assert(console_locks==1 && window_locks==1 && lock_manager==1 && lock_uid==1000);
    account_tick();assert(console_locks==1); /* A locked tick must not flush new login input. */
    assert(!run(NV_ACCOUNT_LOGIN,&admin));
    policy->idle_timeout_ticks=NV_AUTH_IDLE_DEFAULT;assert(!account_ioctl(NV_ACCOUNT_POLICY_SET,(uptr)policy));
    assert(!memcmp(database.records[0].salt,"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10",16));
    /* Python hashlib independent PBKDF2-HMAC-SHA256, same 600,000 rounds. */
    static const u8 expected[32]={0x12,0x08,0xa5,0xff,0x67,0x5c,0x39,0x47,0x7f,0xf2,0xdd,0xcc,0x61,0xef,0x08,0xc6,0x4f,0xcf,0x41,0xaa,0xb0,0x7d,0x97,0xec,0x74,0x6b,0xb9,0x10,0x94,0x68,0xa7,0xde};
    assert(!memcmp(database.records[0].hash,expected,32));
    assert(!account_job.active && !memcmp(&account_job,&(__typeof__(account_job)){0},sizeof(account_job)));
    assert(fs_open(current,ACCOUNT_DATABASE,NV_READ)==-NV_EACCESS);
    assert(fs_open(current,"C:/.system/accounts.db",NV_READ)==-NV_EACCESS);
    assert(fs_open(current,"/home/users/alice/../../.system/accounts.db",NV_READ)==-NV_EACCESS);
    struct nv_stat64 path_stat={.size=0x12345678};
    assert(fs_stat_path64(current,ACCOUNT_DATABASE,&path_stat)==-NV_EACCESS && path_stat.size==0x12345678);
    assert(fs_stat_path64(current,"C:/.system/accounts.db",&path_stat)==-NV_EACCESS);
    assert(!fs_stat_path64(current,"/home/users/alice",&path_stat) && path_stat.kind==NV_DIR);
    assert(fs_authorize(current,0,"C:/.system",2)==-NV_EACCESS);
    assert(run(NV_ACCOUNT_SETUP,&admin)==-NV_EACCESS);
    struct nv_account_request bob=credentials("bob","another long password");assert(!run(NV_ACCOUNT_ADD,&bob));
    assert(database.count==2 && memcmp(database.records[0].salt,database.records[1].salt,16));
    struct nv_account_request disabled={.uid=1001,.flags=1};strlcpy(disabled.display_name,"Bob",64);
    assert(!call(NV_ACCOUNT_UPDATE,&disabled));
    u32 released=network_releases;
    assert(!call(NV_ACCOUNT_LOGOUT,NULL));
    assert(network_releases==released+1 && released_uid==1000 && current->pid==1 &&
        current->uid==NV_UID_NONE); /* Same recovery PID, different future identity. */
    /* A desktop can take over from recovery Loom, while PID 1 survives.
     * Logging out must clear both survivors, not only the current manager. */
    tasks[1]=(struct task){.pid=2,.parent=1,.uid=NV_UID_NONE,.state=NV_READY};
    tasks[1].program_node=make_node(fs_lookup(0,"/apps"),"desktop",NV_FILE,true);
    assert(tasks[1].program_node>=0);actor(1,NV_UID_NONE);
    assert(!call(NV_ACCOUNT_CLAIM,NULL) && manager_pid==2);
    assert(!run(NV_ACCOUNT_LOGIN,&admin));
    released=network_releases;released_pids=0;
    assert(!call(NV_ACCOUNT_LOGOUT,NULL));
    assert(network_releases==released+2 && released_pids==((1u<<1)|(1u<<2)) &&
        released_uid==1000 && current->uid==NV_UID_NONE && tasks[0].uid==NV_UID_NONE);
    account_task_release(2);actor(0,NV_UID_NONE);assert(!call(NV_ACCOUNT_CLAIM,NULL));
    assert(run(NV_ACCOUNT_LOGIN,&bob)==-NV_EACCESS);assert(!run(NV_ACCOUNT_LOGIN,&admin));
    disabled.flags=0;assert(!call(NV_ACCOUNT_UPDATE,&disabled));session_ends=0;
    struct nv_account_request edit={.uid=1000,.role=0};strlcpy(edit.display_name,"Alice",64);
    assert(call(NV_ACCOUNT_UPDATE,&edit)==-NV_EACCESS); /* Keep the last administrator. */
    u32 locks=console_locks;
    assert(!call(NV_ACCOUNT_LOCK,NULL));
    assert(console_locks==locks+1 && window_locks==console_locks && lock_manager==manager_pid && lock_uid==1000);
    assert(!call(NV_ACCOUNT_LOCK,NULL) && console_locks==locks+1);
    struct task locked_app={.pid=9,.uid=1000};
    assert(!account_interactive_allowed(&locked_app) && account_interactive_allowed(current));
    char key;assert(fs_read(&locked_app,0,&key,1)==-NV_EACCESS);
    assert(fs_write(&locked_app,1,"spoof",5)==-NV_EACCESS);
    for (u32 i=0;i<NV_OPEN_MAX;++i) locked_app.fd[i].node=-1;
    int console=fs_open(&locked_app,"/dev/console",NV_READ|NV_WRITE);
    assert(console>=3 && fs_write(&locked_app,console,"spoof",5)==-NV_EACCESS);
    assert(fs_read(&locked_app,console,&key,1)==-NV_EACCESS);fs_close(&locked_app,console);
    assert(run(NV_ACCOUNT_LOGIN,&bob)==-NV_EACCESS); /* Locked identity cannot switch. */
    assert(!run(NV_ACCOUNT_LOGIN,&admin));
    locks=console_locks;
    assert(!call(NV_ACCOUNT_LOGOUT,NULL));assert(session_ends==1 && current->uid==NV_UID_NONE);
    assert(console_locks==locks+1 && window_locks==console_locks && lock_manager==manager_pid && lock_uid==1000);
    assert(fs_open(current,"/home/users/alice",NV_READ)==-NV_EACCESS);
    ticks=0xffffff9cu; /* The first delay straddles the tick counter wrap. */
    for (u32 i=0;i<5;++i) {
        struct nv_account_request wrong=credentials("alice","wrong");assert(run(NV_ACCOUNT_LOGIN,&wrong)==-NV_EACCESS);
    }
    assert(run(NV_ACCOUNT_LOGIN,&bob)==-NV_EAGAIN);
    get_info(&info);assert(info.cooldown_ticks==200);
    ticks+=199;get_info(&info);assert(info.cooldown_ticks==1);
    assert(call(NV_ACCOUNT_LOGIN,&bob)==-NV_EAGAIN && !account_job.active);
    ++ticks;get_info(&info);assert(!info.cooldown_ticks);
    assert(!run(NV_ACCOUNT_LOGIN,&bob));assert(current->uid==1001);
    assert(!account_admin_allowed(current) && account_session_allowed(current));
    assert(fs_open(current,"/home/users/alice",NV_READ)==-NV_EACCESS);
    assert(fs_authorize(current,0,"/home/users/bob/../alice/private",1)==-NV_EACCESS);
    assert(fs_stat_path64(current,"/home/users/bob/../alice",&path_stat)==-NV_EACCESS);
    assert(!fs_stat_path64(current,"/home/users/bob",&path_stat) && path_stat.kind==NV_DIR);
    assert(fs_authorize(current,0,"/home/users/bob/own",1)==0);
    assert(fs_authorize(current,0,"/home/users",1)==-NV_EACCESS);
    edit=(struct nv_account_request){.uid=1001};strlcpy(edit.display_name,"Bob",64);
    assert(!call(NV_ACCOUNT_UPDATE,&edit));edit.role=1;assert(call(NV_ACCOUNT_UPDATE,&edit)==-NV_EACCESS);
    assert(run(NV_ACCOUNT_ADD,&admin)==-NV_EACCESS);
    edit=credentials("bob","replacement local password");edit.uid=1001;
    strlcpy(edit.old_password,"incorrect",sizeof(edit.old_password));assert(run(NV_ACCOUNT_PASSWORD,&edit)==-NV_EACCESS);
    strlcpy(edit.old_password,bob.password,sizeof(edit.old_password));assert(!run(NV_ACCOUNT_PASSWORD,&edit));
    assert(!call(NV_ACCOUNT_LOGOUT,NULL));assert(run(NV_ACCOUNT_LOGIN,&bob)==-NV_EACCESS);
    bob=edit;assert(!run(NV_ACCOUNT_LOGIN,&bob));
    /* Changing a password must not bypass the password verification delay. */
    edit=credentials("bob","a second replacement password");edit.uid=1001;
    strlcpy(edit.old_password,"incorrect",sizeof(edit.old_password));
    for (u32 i=0;i<5;++i) assert(run(NV_ACCOUNT_PASSWORD,&edit)==-NV_EACCESS);
    assert(call(NV_ACCOUNT_PASSWORD,&edit)==-NV_EAGAIN);
    assert(!account_job.active);
    get_info(&info);assert(info.cooldown_ticks==200);
    ticks+=0x80000001u;get_info(&info);assert(!info.cooldown_ticks);
    account_tick();assert(locked);assert(!run(NV_ACCOUNT_LOGIN,&bob));
    strlcpy(edit.old_password,bob.password,sizeof(edit.old_password));
    assert(!run(NV_ACCOUNT_PASSWORD,&edit));bob=edit;
    assert(!failures);get_info(&info);assert(!info.cooldown_ticks);
    /* Reboot clears the session and loads the stored database. */
    account_init();assert(database.count==2 && session_uid==NV_UID_NONE);assert(!call(NV_ACCOUNT_CLAIM,NULL));
    assert(!run(NV_ACCOUNT_LOGIN,&admin));
    edit=(struct nv_account_request){.uid=1001};assert(!call(NV_ACCOUNT_REMOVE,&edit));
    assert(fs_lookup(0,"/home/users/bob")>=0 && run(NV_ACCOUNT_ADD,&bob)==-NV_EEXIST);
    struct task outsider={.pid=3,.uid=1000,.parent=2};current=&outsider;
    assert(call(NV_ACCOUNT_CLAIM,NULL)==-NV_EBUSY && call(NV_ACCOUNT_REMOVE,&edit)==-NV_EACCESS);
    actor(0,1000);assert(account_ioctl(NV_ACCOUNT_INFO,1)==-NV_EFAULT);
    u32 ended=session_ends;
    locks=console_locks;
    account_task_release(1);
    assert(!manager_pid && locked && session_ends==(int)ended+1 && !account_session_allowed(current));
    assert(console_locks==locks+1 && window_locks==console_locks && !lock_manager && lock_uid==1000);
    assert(!call(NV_ACCOUNT_CLAIM,NULL) && locked);
    assert(!run(NV_ACCOUNT_LOGIN,&admin));
    /* Device waits enable timer interrupts during the final account commit. */
    struct nv_account_request carol=credentials("carol","carol local password");
    u32 new_uid=database.next_uid;
    lock_during_sync=true;
    assert(!run(NV_ACCOUNT_ADD,&carol));
    assert(((struct nv_account_progress *)io_buffer)->uid==new_uid);
    assert(locked && !account_job.active && account_index(new_uid)<database.count);
    assert(!run(NV_ACCOUNT_LOGIN,&admin));
    edit=credentials("alice","changed admin password");edit.uid=1000;
    strlcpy(edit.old_password,admin.password,sizeof(edit.old_password));
    lock_during_sync=true;assert(!run(NV_ACCOUNT_PASSWORD,&edit));
    assert(((struct nv_account_progress *)io_buffer)->uid==1000 && locked && !account_job.active);
    assert(run(NV_ACCOUNT_LOGIN,&admin)==-NV_EACCESS);
    admin=edit;assert(!run(NV_ACCOUNT_LOGIN,&admin));
    /* Both committed identities survive a reload; completion never unlocks. */
    account_init();assert(!broken && database.count==2 && account_index(new_uid)<database.count);
    assert(!call(NV_ACCOUNT_CLAIM,NULL) && !run(NV_ACCOUNT_LOGIN,&carol));
    assert(session_uid==new_uid);assert(!call(NV_ACCOUNT_LOGOUT,NULL));
    assert(!run(NV_ACCOUNT_LOGIN,&admin));
    /* An ambiguous disk commit cannot claim success, and reboot reloads it. */
    sync_error=-NV_EIO;edit=(struct nv_account_request){.uid=1000,.role=1};strlcpy(edit.display_name,"Saved Alice",64);
    assert(call(NV_ACCOUNT_UPDATE,&edit)==-NV_EIO && broken);assert(run(NV_ACCOUNT_ADD,&bob)==-NV_EIO);
    sync_error=0;account_init();assert(!broken && !strcmp(database.records[0].display_name,"Saved Alice"));
    struct task internal=internal_task();int fd=fs_open(&internal,ACCOUNT_DATABASE,NV_WRITE);
    assert(fd>=3 && fs_write(&internal,fd,"BROKEN!",7)==7);fs_close(&internal,fd);
    account_init();assert(broken);assert(!call(NV_ACCOUNT_CLAIM,NULL));assert(run(NV_ACCOUNT_SETUP,&admin)==-NV_EIO);
    test_mode=true;account_init();assert(account_ioctl(NV_ACCOUNT_INFO,(u32)(uptr)io_buffer)==-NV_ENODEV);
    puts("PASS accounts: trusted program identity, roles, kernel idle lock, full-cost hash, persistence, ADD/PASSWORD commit across lock, login/password cooldown, tick wrap, home isolation and fail-closed storage");
    return 0;
}
