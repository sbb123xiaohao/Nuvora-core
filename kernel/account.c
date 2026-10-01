#include "kernel.h"
#include <nv/crypto.h>

#define ACCOUNT_ROUNDS 600000u
#define ACCOUNT_DATABASE "/home/.system/accounts.db"
#define ACCOUNT_TEMP "/home/.system/.accounts-new"
struct account_record {
    u32 uid,role,flags,rounds;
    char name[32],display_name[64];
    u8 salt[16],hash[32];
};
struct account_database {
    char magic[8];
    u32 version,count,next_uid,reserved;
    u8 checksum[32];
    struct account_record records[NV_ACCOUNT_MAX];
};
static struct account_database database,candidate;
static bool enabled,broken,locked;
static u32 manager_pid,session_uid=NV_UID_NONE,failures,retry_delay;
static u64 retry_started;
static u64 last_activity;
static u32 idle_timeout=NV_AUTH_IDLE_DEFAULT;
static struct {
    bool active,verifying;
    u32 op,target,verify_index,completed;
    struct nv_account_request request;
    struct account_record record;
    struct nv_pbkdf2 hash;
} account_job;

static bool prefix(const char *path,const char *part) {
    usize n=strlen(part);
    return !strncmp(path,part,n) && (!path[n] || path[n]=='/');
}
static u32 account_index(u32 uid) {
    for (u32 i=0;i<database.count;++i) if (database.records[i].uid==uid) return i;
    return NV_ACCOUNT_MAX;
}
static u32 name_index(const char *name) {
    for (u32 i=0;i<database.count;++i) if (!strcmp(database.records[i].name,name)) return i;
    return NV_ACCOUNT_MAX;
}
static bool valid_name(const char *name) {
    u32 n=(u32)strnlen(name,32);
    if (!n || n>NV_NAME_MAX || name[0]<'a' || name[0]>'z') return false;
    for (u32 i=1;i<n;++i)
        if (!((name[i]>='a' && name[i]<='z') || (name[i]>='0' && name[i]<='9') ||
            name[i]=='-' || name[i]=='_')) return false;
    return true;
}
static bool valid_label(const char *name) {
    u32 n=(u32)strnlen(name,64);
    if (!n || n>=64) return false;
    for (u32 i=0;i<n;++i) if ((u8)name[i]<32 || (u8)name[i]>126) return false;
    return true;
}
static bool password_input(const char *p,bool creating) {
    u32 n=(u32)strnlen(p,NV_PASSWORD_MAX+1);
    if (n>NV_PASSWORD_MAX || (creating && n<NV_PASSWORD_MIN)) return false;
    for (u32 i=0;i<n;++i) if ((u8)p[i]<32 || (u8)p[i]>126) return false;
    if (creating) {
        bool different=false;
        for (u32 i=1;i<n;++i) if (p[i]!=p[0]) different=true;
        if (!different) return false;
        static const char *const common[]={"passwordpassword","123456789012345","qwertyuiopasdfgh"};
        for (u32 i=0;i<ARRAY_LEN(common);++i) if (!strcmp(p,common[i])) return false;
    }
    return true;
}
static void home_path(char out[NV_PATH_MAX],const char *name) {
    strlcpy(out,"/home/users/",NV_PATH_MAX);strlcpy(out+12,name,NV_PATH_MAX-12);
}
static bool administrator(void) {
    u32 i=account_index(session_uid);
    return !locked && current && current->uid==session_uid && i<database.count &&
        database.records[i].role==NV_ACCOUNT_ADMIN;
}
bool account_manager(const struct task *t) {
    return t && t->pid==manager_pid;
}
void account_tick(void) {
    if (enabled && !locked && session_uid!=NV_UID_NONE && ticks-last_activity>=idle_timeout) {
        locked=true;
        nv_secret_clear(&account_job,sizeof(account_job));
    }
}
void account_input_activity(void) {
    account_tick();
    if (enabled && !locked && session_uid!=NV_UID_NONE) last_activity=ticks;
}
bool account_admin_allowed(const struct task *t) {
    account_tick();
    u32 i=t?account_index(t->uid):NV_ACCOUNT_MAX;
    return !enabled || (t && !locked && !broken && t->uid==session_uid &&
        i<database.count && !database.records[i].flags && database.records[i].role==NV_ACCOUNT_ADMIN);
}
bool account_task_allowed(const struct task *t) {
    return !enabled || (t && (t->auth_internal ||
        (t->uid!=NV_UID_NONE && account_index(t->uid)<database.count &&
         !(database.records[account_index(t->uid)].flags&NV_ACCOUNT_DISABLED))));
}
bool account_interactive_allowed(const struct task *t) {
    account_tick();
    return !enabled || account_manager(t) || (!locked && account_task_allowed(t));
}
bool account_session_allowed(const struct task *t) {
    account_tick();
    return !enabled || (!locked && account_task_allowed(t));
}
bool account_path_allowed(const struct task *t,const char *path,u32 operation) {
    if (!enabled || (t && t->auth_internal)) return true;
    /* Protect the object, not a spelling: fs_authorize resolves C:/, '..'
     * and relative aliases to the canonical node before calling this. */
    if (prefix(path,"/home/.system")) return false;
    if (!account_task_allowed(t))
        return operation==0 && (!strcmp(path,"/") || prefix(path,"/apps") ||
            prefix(path,"/dev") || prefix(path,"/sys"));
    if (operation && (!strcmp(path,"/home/users") ||
        (operation==2 && !strcmp(path,"/home")))) return false;
    if (prefix(path,"/home/users") && path[11]=='/') {
        const char *name=path+12;char user[32];u32 n=0;
        while (name[n] && name[n]!='/' && n<31) { user[n]=name[n];++n; }
        user[n]=0;
        if (operation==2 && !name[n]) return false; /* Stable private roots. */
        u32 i=name_index(user),actor=account_index(t->uid);
        return actor<database.count && (database.records[actor].role==NV_ACCOUNT_ADMIN ||
            (i<database.count && database.records[i].uid==t->uid));
    }
    return true;
}
static struct task internal_task(void) {
    struct task t={.auth_internal=true,.uid=NV_UID_NONE};
    for (u32 i=0;i<NV_OPEN_MAX;++i) t.fd[i].node=-1;
    return t;
}
static void database_digest(const struct account_database *db,u8 out[32]) {
    struct nv_sha256 hash;
    nv_sha256_begin(&hash);
    nv_sha256_update(&hash,db,24);
    nv_sha256_update(&hash,db->records,sizeof(db->records));
    nv_sha256_end(&hash,out);
}
static bool database_valid(const struct account_database *db) {
    u8 checksum[32];database_digest(db,checksum);
    bool valid=!memcmp(db->magic,"NVACCT1",8) && db->version==1 && !db->reserved &&
        db->count && db->count<=NV_ACCOUNT_MAX && db->next_uid>=1001 &&
        nv_secret_equal(checksum,db->checksum,32);
    nv_secret_clear(checksum,sizeof(checksum));
    if (!valid) return false;
    u32 admins=0;
    for (u32 i=0;i<db->count;++i) {
        const struct account_record *r=&db->records[i];
        if (r->uid<1000 || r->uid>=db->next_uid || r->role>1 || r->flags>1 ||
            r->rounds!=ACCOUNT_ROUNDS || !valid_name(r->name) || !valid_label(r->display_name)) return false;
        admins+=r->role==NV_ACCOUNT_ADMIN && !(r->flags&NV_ACCOUNT_DISABLED);
        for (u32 j=0;j<i;++j)
            if (db->records[j].uid==r->uid || !strcmp(db->records[j].name,r->name)) return false;
    }
    return admins>0;
}
static int ensure_directory(const char *path) {
    int n=fs_lookup(0,path);
    if (n>=0) return fs_kind(n)==NV_DIR?0:-NV_EIO;
    return n==-NV_ENOENT?fs_mkdir(0,path):n;
}
static int persist_candidate(void) {
    int r=ensure_directory("/home/.system");
    if (r<0) return r;
    database_digest(&candidate,candidate.checksum);
    struct task t=internal_task();
    int fd=fs_open(&t,ACCOUNT_TEMP,NV_WRITE|NV_CREATE|NV_TRUNC);
    if (fd<0) return fd;
    r=fs_write(&t,fd,&candidate,sizeof(candidate));
    int closed=fs_close(&t,fd);
    if (r!=(int)sizeof(candidate)) { fs_remove(0,ACCOUNT_TEMP);return r<0?r:-NV_EIO; }
    if (closed<0) { fs_remove(0,ACCOUNT_TEMP);return closed; }
    r=fs_replace(0,ACCOUNT_TEMP,ACCOUNT_DATABASE);
    if (r<0) { fs_remove(0,ACCOUNT_TEMP);return r; }
    /* Do not publish success or change in-memory identities before the data
     * volume confirms its commit. An ambiguous failure requires a reload. */
    if (disk_ready() && (r=store_sync())<0) { broken=true;return r; }
    database=candidate;
    return 0;
}
void account_init(void) {
    enabled=!test_mode;broken=locked=false;manager_pid=failures=retry_started=retry_delay=0;session_uid=NV_UID_NONE;
    nv_secret_clear(&account_job,sizeof(account_job));
    last_activity=ticks;idle_timeout=NV_AUTH_IDLE_DEFAULT;
    database=(struct account_database){.magic="NVACCT1",.version=1,.next_uid=1000};
    if (!enabled) return;
    struct task t=internal_task();
    int fd=fs_open(&t,ACCOUNT_DATABASE,NV_READ);
    if (fd==-NV_ENOENT) return;
    if (fd<0) { broken=true;return; }
    struct nv_stat64 st;
    int r=fs_stat64(&t,fd,&st);
    if (!r && st.size==sizeof(candidate)) r=fs_read(&t,fd,&candidate,sizeof(candidate));
    else r=-NV_EIO;
    fs_close(&t,fd);
    if (r!=(int)sizeof(candidate) || !database_valid(&candidate)) { broken=true;return; }
    database=candidate;
}
void account_task_release(u32 pid) {
    if (pid!=manager_pid) return;
    nv_secret_clear(&account_job,sizeof(account_job));manager_pid=0;
    if (session_uid!=NV_UID_NONE) locked=true;
    task_end_session(pid);
}
static void session_start(u32 index) {
    session_uid=database.records[index].uid;locked=false;failures=retry_started=retry_delay=0;
    last_activity=ticks;
    current->uid=session_uid;
    char home[NV_PATH_MAX];home_path(home,database.records[index].name);
    int cwd=fs_lookup(0,home);
    if (cwd>=0) current->cwd=cwd;
    for (u32 i=0;i<NV_TASK_MAX;++i) if (tasks[i].pid==1 && tasks[i].state) {
        tasks[i].uid=session_uid;
        if (cwd>=0) tasks[i].cwd=cwd;
    }
}
static u32 cooldown_remaining(void) {
    if (!retry_delay) return 0;
    /* Elapsed unsigned ticks cross the counter wrap without interpreting an
     * inactive or expired deadline as a future wait at long uptimes. */
    u64 elapsed=ticks-retry_started;
    if (elapsed>=retry_delay) { retry_delay=0;return 0; }
    return retry_delay-elapsed;
}
static void failed_verification(void) {
    failures=MIN(failures+1,10u);
    if (failures>=5) {
        retry_started=ticks;retry_delay=MIN(6000u,200u<<(failures-5));
    }
}
static int begin_job(u32 op,const struct nv_account_request *input) {
    if (account_job.active) return -NV_EBUSY;
    if (broken) return -NV_EIO;
    struct nv_account_request request=*input;
    if (strnlen(request.name,32)==32 || strnlen(request.display_name,64)==64 ||
        !password_input(request.password,false) || !password_input(request.old_password,false)) {
        nv_secret_clear(&request,sizeof(request));return -NV_EINVAL;
    }
    u32 target=NV_ACCOUNT_MAX,verify=NV_ACCOUNT_MAX;
    int error=0;
    if (op==NV_ACCOUNT_SETUP || op==NV_ACCOUNT_ADD) {
        if (op==NV_ACCOUNT_SETUP ? database.count>0 : !administrator()) error=-NV_EACCESS;
        else if (database.count==NV_ACCOUNT_MAX || database.next_uid==NV_UID_NONE) error=-NV_ENOSPC;
        else if (!valid_name(request.name) || !valid_label(request.display_name) ||
                 !password_input(request.password,true) || request.role>1) error=-NV_EINVAL;
        else if (name_index(request.name)<NV_ACCOUNT_MAX) error=-NV_EEXIST;
        else {
            char home[NV_PATH_MAX];home_path(home,request.name);
            if (fs_lookup(0,home)>=0) error=-NV_EEXIST; /* Never inherit another user's kept files. */
        }
    } else if (op==NV_ACCOUNT_LOGIN) {
        if (!database.count || (session_uid!=NV_UID_NONE && !locked)) error=-NV_EBUSY;
        else if (cooldown_remaining()) error=-NV_EAGAIN;
        target=name_index(request.name);verify=target;
        if (locked && target<database.count && database.records[target].uid!=session_uid)
            target=verify=NV_ACCOUNT_MAX;
    } else if (op==NV_ACCOUNT_PASSWORD) {
        target=account_index(request.uid);verify=account_index(session_uid);
        if (locked || verify==NV_ACCOUNT_MAX || target==NV_ACCOUNT_MAX ||
            (request.uid!=session_uid && !administrator())) error=-NV_EACCESS;
        else if (!password_input(request.password,true)) error=-NV_EINVAL;
        else if (cooldown_remaining()) error=-NV_EAGAIN;
    } else error=-NV_EINVAL;
    if (error) { nv_secret_clear(&request,sizeof(request));return error; }
    nv_secret_clear(&account_job,sizeof(account_job));
    account_job.op=op;account_job.target=target;account_job.verify_index=verify;
    account_job.request=request;account_job.active=true;
    nv_secret_clear(&request,sizeof(request));
    if (op==NV_ACCOUNT_LOGIN || op==NV_ACCOUNT_PASSWORD) {
        static const u8 dummy[16]={0x4e,0x75,0x76,0x6f,0x72,0x61};
        const u8 *salt=verify<database.count?database.records[verify].salt:dummy;
        const char *password=op==NV_ACCOUNT_LOGIN?account_job.request.password:account_job.request.old_password;
        nv_pbkdf2_begin(&account_job.hash,password,strlen(password),salt,16,ACCOUNT_ROUNDS);
        account_job.verifying=true;
    } else {
        struct account_record *record=&account_job.record;
        *record=(struct account_record){.uid=database.next_uid,.role=op==NV_ACCOUNT_SETUP?1:input->role,.rounds=ACCOUNT_ROUNDS};
        strlcpy(record->name,input->name,32);strlcpy(record->display_name,input->display_name,64);
        if (!cpu_random(record->salt,16)) {
            nv_secret_clear(&account_job,sizeof(account_job));return -NV_ENODEV;
        }
        nv_pbkdf2_begin(&account_job.hash,account_job.request.password,strlen(account_job.request.password),
            record->salt,16,ACCOUNT_ROUNDS);
    }
    return 0;
}
static int poll_job(struct nv_account_progress *out) {
    if (!account_job.active) return -NV_ENOENT;
    u32 total=ACCOUNT_ROUNDS*(account_job.op==NV_ACCOUNT_PASSWORD?2u:1u);
    *out=(struct nv_account_progress){.completed=account_job.completed+account_job.hash.done,.total=total,.uid=NV_UID_NONE};
    if (!nv_pbkdf2_step(&account_job.hash,4096)) {
        out->completed=account_job.completed+account_job.hash.done;return 0;
    }
    int result=0;u32 uid=NV_UID_NONE;
    if (account_job.verifying) {
        u32 i=account_job.verify_index;
        bool matches=i<database.count && !(database.records[i].flags&NV_ACCOUNT_DISABLED) &&
            nv_secret_equal(account_job.hash.result,database.records[i].hash,32);
        if (!matches) { failed_verification();result=-NV_EACCESS;goto complete; }
        failures=retry_started=retry_delay=0;
        if (account_job.op==NV_ACCOUNT_LOGIN) {
            session_start(account_job.target);uid=session_uid;goto complete;
        }
        account_job.verifying=false;account_job.completed=ACCOUNT_ROUNDS;
        account_job.record=database.records[account_job.target];
        if (!cpu_random(account_job.record.salt,16)) { result=-NV_ENODEV;goto complete; }
        nv_pbkdf2_begin(&account_job.hash,account_job.request.password,strlen(account_job.request.password),
            account_job.record.salt,16,ACCOUNT_ROUNDS);
        return 0;
    }
    memcpy(account_job.record.hash,account_job.hash.result,32);
    candidate=database;
    if (account_job.op==NV_ACCOUNT_PASSWORD) candidate.records[account_job.target]=account_job.record;
    else {
        char home[NV_PATH_MAX];home_path(home,account_job.record.name);
        result=ensure_directory("/home/users");
        if (!result) result=fs_mkdir(0,home);
        if (result<0) goto complete;
        candidate.records[candidate.count++]=account_job.record;++candidate.next_uid;
    }
    result=persist_candidate();
    if (result<0 && !broken && account_job.op!=NV_ACCOUNT_PASSWORD) {
        char home[NV_PATH_MAX];home_path(home,account_job.record.name);fs_remove(0,home);
    }
    if (!result) {
        uid=account_job.record.uid;
        if (account_job.op==NV_ACCOUNT_SETUP) session_start(database.count-1);
    }
complete:
    *out=(struct nv_account_progress){.completed=total,.total=total,.uid=uid,.result=result};
    nv_secret_clear(&account_job,sizeof(account_job));
    return 1;
}
static int update_account(u32 op,const struct nv_account_request *io) {
    if (broken || locked || account_job.active || session_uid==NV_UID_NONE) return -NV_EACCESS;
    u32 index=account_index(io->uid);
    if (index==NV_ACCOUNT_MAX || io->role>1 || io->flags>1) return -NV_EINVAL;
    if (!administrator() && (op!=NV_ACCOUNT_UPDATE || io->uid!=session_uid ||
        io->role!=database.records[index].role || io->flags!=database.records[index].flags)) return -NV_EACCESS;
    if (io->uid==session_uid && (op==NV_ACCOUNT_REMOVE || (io->flags&NV_ACCOUNT_DISABLED))) return -NV_EBUSY;
    candidate=database;
    if (op==NV_ACCOUNT_REMOVE) {
        memmove(candidate.records+index,candidate.records+index+1,(candidate.count-index-1)*sizeof(struct account_record));
        memset(candidate.records+--candidate.count,0,sizeof(struct account_record));
    } else {
        if (!valid_label(io->display_name)) return -NV_EINVAL;
        candidate.records[index].role=io->role;candidate.records[index].flags=io->flags;
        strlcpy(candidate.records[index].display_name,io->display_name,64);
    }
    u32 admins=0;
    for (u32 i=0;i<candidate.count;++i) admins+=candidate.records[i].role==1 && !candidate.records[i].flags;
    if (!admins) return -NV_EACCESS;
    return persist_candidate(); /* Kept home files are not deleted with an account. */
}
static void get_info(struct nv_account_info *out) {
    *out=(struct nv_account_info){.api_version=NV_ACCOUNT_API_VERSION,.count=database.count,
        .uid=session_uid,.manager_pid=manager_pid,.flags=(!database.count?NV_AUTH_SETUP:0u)|
        (disk_ready()?NV_AUTH_PERSISTENT:0u)|(session_uid!=NV_UID_NONE?NV_AUTH_SIGNED_IN:0u)|
        (locked?NV_AUTH_LOCKED:0u)|(broken?NV_AUTH_ERROR:0u)|(account_job.active?NV_AUTH_BUSY:0u),
        .cooldown_ticks=cooldown_remaining()};
    u32 i=account_index(session_uid);
    if (i<database.count) {
        out->role=database.records[i].role;
        strlcpy(out->name,database.records[i].name,32);
        strlcpy(out->display_name,database.records[i].display_name,64);
        home_path(out->home,database.records[i].name);
    }
}
int account_ioctl(u32 op,uptr pointer) {
    if (!enabled) return -NV_ENODEV;
    account_tick();
    if (op==NV_ACCOUNT_CLAIM) {
        int node=fs_lookup(0,"/apps/desktop");
        bool desktop=node>=0 && current->parent==1 && current->program_node==node;
        node=fs_lookup(0,"/apps/loom");
        bool recovery=node>=0 && current->pid==1 && current->program_node==node;
        if (pointer || (manager_pid && !account_manager(current) && !(manager_pid==1 && desktop && !account_job.active)))
            return -NV_EBUSY;
        if (!recovery && !desktop)
            return -NV_EACCESS;
        manager_pid=current->pid;return 0;
    }
    u32 bytes=op==NV_ACCOUNT_INFO?sizeof(struct nv_account_info):
        op==NV_ACCOUNT_LIST?sizeof(struct nv_account_profile):op==NV_ACCOUNT_POLL?sizeof(struct nv_account_progress):
        (op==NV_ACCOUNT_POLICY_GET || op==NV_ACCOUNT_POLICY_SET)?sizeof(struct nv_account_policy):
        (op==NV_ACCOUNT_SETUP || op==NV_ACCOUNT_LOGIN || op==NV_ACCOUNT_ADD ||
         op==NV_ACCOUNT_PASSWORD || op==NV_ACCOUNT_REMOVE || op==NV_ACCOUNT_UPDATE)?sizeof(struct nv_account_request):0;
    bool writes=op==NV_ACCOUNT_INFO || op==NV_ACCOUNT_LIST || op==NV_ACCOUNT_POLL || op==NV_ACCOUNT_POLICY_GET;
    if (bytes?!user_range(current->pd,pointer,bytes,writes):pointer!=0) return -NV_EFAULT;
    if (op==NV_ACCOUNT_INFO) {
        struct nv_account_info out;get_info(&out);memcpy((void *)(uptr)pointer,&out,sizeof(out));return 0;
    }
    if (op==NV_ACCOUNT_LIST) {
        struct nv_account_profile out;memcpy(&out,(void *)(uptr)pointer,sizeof(out));
        if (out.index>=database.count) return 0;
        const struct account_record *r=&database.records[out.index];
        out=(struct nv_account_profile){.index=out.index,.uid=r->uid,.role=r->role,.flags=r->flags};
        strlcpy(out.name,r->name,32);strlcpy(out.display_name,r->display_name,64);
        memcpy((void *)(uptr)pointer,&out,sizeof(out));return 1;
    }
    if (!account_manager(current)) return -NV_EACCESS;
    if (op==NV_ACCOUNT_POLICY_GET || op==NV_ACCOUNT_POLICY_SET) {
        if (op==NV_ACCOUNT_POLICY_SET) {
            if (broken || locked || session_uid==NV_UID_NONE) return -NV_EACCESS;
            struct nv_account_policy policy;memcpy(&policy,(void *)pointer,sizeof(policy));
            if (policy.idle_timeout_ticks<NV_AUTH_IDLE_MIN || policy.idle_timeout_ticks>NV_AUTH_IDLE_MAX)
                return -NV_EINVAL;
            idle_timeout=policy.idle_timeout_ticks;
            account_tick();
        } else {
            struct nv_account_policy policy={idle_timeout,(u32)MIN(ticks-last_activity,(u64)0xffffffffu)};
            memcpy((void *)pointer,&policy,sizeof(policy));
        }
        return 0;
    }
    if (op==NV_ACCOUNT_CANCEL) { nv_secret_clear(&account_job,sizeof(account_job));return 0; }
    if (op==NV_ACCOUNT_LOCK) {
        if (broken || session_uid==NV_UID_NONE || account_job.active) return -NV_EACCESS;
        locked=true;return 0;
    }
    if (op==NV_ACCOUNT_LOGOUT) {
        task_end_session(manager_pid);nv_secret_clear(&account_job,sizeof(account_job));
        current->uid=session_uid=NV_UID_NONE;locked=false;
        for (u32 i=0;i<NV_TASK_MAX;++i) if (tasks[i].pid==1) tasks[i].uid=NV_UID_NONE;
        return 0;
    }
    if (op==NV_ACCOUNT_POLL) {
        struct nv_account_progress out;int r=poll_job(&out);
        if (r>=0) memcpy((void *)(uptr)pointer,&out,sizeof(out));
        return r;
    }
    if (bytes==sizeof(struct nv_account_request)) {
        struct nv_account_request io;memcpy(&io,(void *)(uptr)pointer,sizeof(io));
        int result=(op==NV_ACCOUNT_UPDATE || op==NV_ACCOUNT_REMOVE)?update_account(op,&io):begin_job(op,&io);
        nv_secret_clear(&io,sizeof(io));return result;
    }
    return -NV_EINVAL;
}
