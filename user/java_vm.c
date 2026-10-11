#include "java_vm.h"
#include <nv/string.h>

/* No libc, executable heap, host pointers in Java values, or recursive C
 * dispatch. All limits also apply when the interpreter is used by host tests. */
#define CP_MAX 1024
#define METHOD_MAX 128
#define FIELD_MAX 64
#define FRAME_MAX 32
#define SLOT_MAX 256
#define OBJECT_MAX 4096
#define STRING_MAX 4096
#define ARG_MAX 32
#define BOOT_MAX 32
#define CODE_MAX 16384
enum { T_EMPTY, T_INT, T_LONG, T_REF, T_TOP, T_VOID };
enum { O_INSTANCE, O_STRING, O_PRINT, O_ARRAY };
enum { C_OBJECT, C_RESERVED, C_STRING, C_PRINT, C_INTS, C_LONGS,
       C_BYTES, C_CHARS, C_SHORTS, C_BOOLS, C_OBJECTS, C_RESERVED_ARRAY, C_STRINGS,
       C_APP=32, C_APP_ARRAY=64 };
struct value { u64 bits; u8 tag; };
struct cp { const u8 *text; u64 bits; u16 length, a, b; u8 tag; };
struct method {
    const u8 *code;
    u8 *starts;
    u32 length;
    u16 flags, name, desc, stack, locals;
};
struct field { struct value value; u16 flags, name, desc, constant; };
struct object { u32 length; u8 kind, cls; u64 data[]; };
struct frame {
    struct value locals[SLOT_MAX], stack[SLOT_MAX];
    u32 pc;
    u16 method, used;
    u8 owner;
    bool initializer;
};
struct bootstrap { u16 handle, count, args[ARG_MAX]; };
struct java_class {
    struct cp pool[CP_MAX];
    u32 strings[CP_MAX];
    struct method methods[METHOD_MAX];
    struct field fields[FIELD_MAX];
    struct bootstrap boots[BOOT_MAX];
    u16 pool_count, method_count, field_count, boot_count, self, major, flags;
    u8 id, state; /* 0 prepared, 1 initializing on this thread, 2 initialized */
};
struct vm {
    struct java_class *c, *classes[NV_JAVA_CLASSES];
    struct frame frames[FRAME_MAX];
    struct object *objects[OBJECT_MAX];
    u16 text_buffer[STRING_MAX];
    u8 *arena;
    u32 capacity, used, object_count, depth, limit, class_count;
    struct nv_java_host host;
    struct nv_java_result *result;
};
struct reader { const u8 *bytes; u32 size, at; struct vm *vm; };
struct signature { u8 args[ARG_MAX], count, result; u16 slots, positions[ARG_MAX], return_at; };

static bool fail(struct vm *v, enum nv_java_status code, const char *message) {
    if (!v->result->status) { v->result->status = code; v->result->message = message; }
    return false;
}
static bool good(struct vm *v) { return !v->result->status; }
static void *allocate(struct vm *v, u32 bytes) {
    if (bytes > v->capacity - v->used || bytes > 0xfffffff0u) {
        fail(v, NV_JAVA_MEMORY, "Java arena exhausted (no GC in this bootstrap)"); return NULL;
    }
    u32 rounded = (bytes + 15u) & ~15u;
    if (rounded > v->capacity - v->used) {
        fail(v, NV_JAVA_MEMORY, "Java arena exhausted"); return NULL;
    }
    void *p = v->arena + v->used;
    memset(p, 0, rounded); v->used += rounded;
    return p;
}
static u32 read_n(struct reader *r, u32 count) {
    if (count > r->size - r->at) {
        fail(r->vm, NV_JAVA_FORMAT, "Truncated class file"); return 0;
    }
    u32 n = 0;
    for (u32 i = 0; i < count; ++i) n = (n << 8) | r->bytes[r->at++];
    return n;
}
static bool skip(struct reader *r, u32 n) {
    if (n > r->size - r->at) return fail(r->vm, NV_JAVA_FORMAT, "Attribute exceeds class file");
    r->at += n; return true;
}
static struct cp *constant(struct vm *v, u16 index, u8 tag) {
    if (!index || index >= v->c->pool_count || v->c->pool[index].tag != tag) {
        fail(v, NV_JAVA_FORMAT, "Invalid constant-pool reference"); return NULL;
    }
    return &v->c->pool[index];
}
static bool text_is(struct vm *v, u16 index, const char *text) {
    struct cp *c = constant(v, index, 1);
    return c && c->length == strlen(text) && !memcmp(c->text, text, c->length);
}
static bool text_same(struct vm *v, u16 a, u16 b) {
    struct cp *x = constant(v, a, 1), *y = constant(v, b, 1);
    return x && y && x->length == y->length && !memcmp(x->text, y->text, x->length);
}
static bool class_is(struct vm *v, u16 index, const char *name) {
    struct cp *c = constant(v, index, 7);
    return c && text_is(v, c->a, name);
}
/* Modified UTF-8 represents UTF-16 units, including isolated surrogates. */
static bool mutf_unit(const u8 *s, u32 length, u32 *at, u16 *unit) {
    if (*at >= length) return false;
    u32 a = s[(*at)++];
    if (a && a < 128) { *unit = a; return true; }
    if (a >= 0xc0 && a <= 0xdf && *at < length) {
        u32 b = s[(*at)++];
        u32 n = ((a & 31) << 6) | (b & 63);
        if ((b & 0xc0) != 0x80 || (n < 128 && !(a == 0xc0 && b == 0x80))) return false;
        *unit = n; return true;
    }
    if (a >= 0xe0 && a <= 0xef && length - *at >= 2) {
        u32 b = s[(*at)++], c = s[(*at)++];
        u32 n = ((a & 15) << 12) | ((b & 63) << 6) | (c & 63);
        if ((b & 0xc0) != 0x80 || (c & 0xc0) != 0x80 || n < 2048) return false;
        *unit = n; return true;
    }
    return false;
}
static bool valid_mutf(const u8 *s, u32 length) {
    u32 at = 0; u16 unit;
    while (at < length) if (!mutf_unit(s, length, &at, &unit)) return false;
    return true;
}
/* Class names become relative paths. In particular, neither a class file nor
 * a symbolic reference may escape the chosen root with dots or backslashes.
 * Surrogates/NUL are not representable filesystem names in this loader. */
static bool valid_class_name(const u8 *s,u32 n) {
    if (!n || n>=NV_JAVA_NAME_MAX) return false;
    u32 at=0;
    while (at<n) {
        u32 start=at; u16 c;
        if (!mutf_unit(s,n,&at,&c) || c<32 || c==127 || (c>=0xd800 && c<=0xdfff) ||
            c=='.' || c=='\\' || c==':' || c==';' || c=='[') return false;
        if (c=='/' && (!start || at==n || s[start-1]=='/')) return false;
    }
    return true;
}
int nv_java_class_path(char *out,u32 capacity,const char *root,const char *name) {
    if (!out || !capacity || !root || !name) return -1;
    out[0]=0;
    u32 n=strlen(name),r=strlen(root);
    if (!valid_class_name((const u8 *)name,n) || r>=capacity || n>=capacity-r) return -1;
    bool slash=r && root[r-1]!='/';
    if (capacity-r-n<7u+(u32)slash) return -1;
    memcpy(out,root,r); if (slash) out[r++]='/';
    memcpy(out+r,name,n); memcpy(out+r+n,".class",7);
    return 0;
}
static u8 descriptor_type(struct vm *v, const u8 *s, u32 length, u32 *at, bool result) {
    if (*at >= length) { fail(v, NV_JAVA_FORMAT, "Truncated descriptor"); return T_EMPTY; }
    u8 c = s[(*at)++];
    if (c == 'V' && result) return T_VOID;
    if (c == 'J') return T_LONG;
    if (c == 'B' || c == 'C' || c == 'S' || c == 'Z' || c == 'I') return T_INT;
    if (c == 'F' || c == 'D') { fail(v, NV_JAVA_UNSUPPORTED, "Floating-point descriptors are not implemented"); return T_EMPTY; }
    if (c == 'L') {
        u32 start = *at;
        while (*at < length && s[*at] != ';') ++*at;
        if (*at == length || *at == start) { fail(v, NV_JAVA_FORMAT, "Invalid object descriptor"); return T_EMPTY; }
        ++*at; return T_REF;
    }
    if (c == '[') {
        u32 dimensions = 1;
        while (*at < length && s[*at] == '[') { ++*at; ++dimensions; }
        if (dimensions > 255 || !descriptor_type(v, s, length, at, false)) return T_EMPTY;
        return T_REF;
    }
    fail(v, NV_JAVA_FORMAT, "Invalid descriptor"); return T_EMPTY;
}
static bool signature(struct vm *v, u16 index, struct signature *out) {
    memset(out, 0, sizeof(*out));
    struct cp *c = constant(v, index, 1);
    if (!c || c->length < 3 || c->text[0] != '(') return fail(v, NV_JAVA_FORMAT, "Invalid method descriptor");
    u32 at = 1;
    while (at < c->length && c->text[at] != ')') {
        if (out->count == ARG_MAX) return fail(v, NV_JAVA_LIMIT, "Method argument limit exceeded");
        out->positions[out->count]=at;
        u8 type = descriptor_type(v, c->text, c->length, &at, false);
        if (!type) return false;
        out->args[out->count++] = type; out->slots += type == T_LONG ? 2 : 1;
    }
    if (at == c->length) return fail(v, NV_JAVA_FORMAT, "Unterminated method descriptor");
    ++at; out->return_at=at; out->result = descriptor_type(v, c->text, c->length, &at, true);
    return good(v) && at == c->length ? true : fail(v, NV_JAVA_FORMAT, "Trailing method descriptor data");
}
static u8 field_type(struct vm *v, u16 index) {
    struct cp *c = constant(v, index, 1);
    u32 at = 0;
    u8 t = c ? descriptor_type(v, c->text, c->length, &at, false) : T_EMPTY;
    if (c && at != c->length) fail(v, NV_JAVA_FORMAT, "Trailing field descriptor data");
    return good(v) ? t : T_EMPTY;
}
static u32 big32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static i64 signed32(u32 n) { return n & 0x80000000u ? (i64)n - 0x100000000ll : n; }
static i64 signed16(u32 n) { return n & 0x8000u ? (i64)n - 65536 : n; }
static i64 signed8(u32 n) { return n & 0x80u ? (i64)n - 256 : n; }
static i64 signed64(u64 n) { return n >> 63 ? -1 - (i64)~n : (i64)n; }
static bool branch_target(struct vm *v, struct method *m, u32 pc, i64 delta) {
    i64 target = (i64)pc + delta;
    return target >= 0 && (u64)target < m->length && m->starts[target] ? true :
           fail(v, NV_JAVA_VERIFY, "Branch does not target an instruction");
}
/* Decode lengths before executing, including unreachable code. This prevents a
 * branch into an operand from manufacturing a different instruction stream. */
static bool decode(struct vm *v, struct method *m) {
    m->starts = allocate(v, m->length);
    if (!m->starts) return false;
    for (u32 pass = 0; pass < 2; ++pass) {
        for (u32 pc = 0; pc < m->length;) {
            const u8 *c = m->code; u8 op = c[pc]; u32 n = 1;
            if (!pass) m->starts[pc] = 1;
            if (op == 0xaa || op == 0xab) {
                u32 p = (pc + 4) & ~3u;
                u32 header = op == 0xaa ? 12 : 8;
                if (p > m->length || header > m->length - p) return fail(v, NV_JAVA_FORMAT, "Truncated switch");
                for (u32 q = pc + 1; q < p; ++q) if (c[q]) return fail(v, NV_JAVA_FORMAT, "Nonzero switch padding");
                i64 count = op == 0xaa ? signed32(big32(c+p+8)) - signed32(big32(c+p+4)) + 1 : signed32(big32(c+p+4));
                u32 stride = op == 0xaa ? 4 : 8;
                if (count < (op == 0xaa ? 1 : 0) || (u64)count > (m->length-p-header)/stride)
                    return fail(v, NV_JAVA_FORMAT, "Invalid switch range");
                if (pass && !branch_target(v,m,pc,signed32(big32(c+p)))) return false;
                i64 last = -2147483649ll;
                for (u32 i = 0; i < (u32)count; ++i) {
                    u32 q = p + header + i * stride;
                    if (op == 0xab) {
                        i64 key = signed32(big32(c+q));
                        if (key <= last) return fail(v,NV_JAVA_FORMAT,"Unsorted lookup switch");
                        last = key; q += 4;
                    }
                    if (pass && !branch_target(v,m,pc,signed32(big32(c+q)))) return false;
                }
                n = p + header + (u32)count*stride - pc;
            } else if (op == 0xc4) {
                if (m->length - pc < 2) return fail(v,NV_JAVA_FORMAT,"Truncated wide instruction");
                u8 sub = c[pc+1];
                if (sub == 0x84) n = 6;
                else if ((sub >= 0x15 && sub <= 0x19) || (sub >= 0x36 && sub <= 0x3a)) n = 4;
                else return fail(v,NV_JAVA_UNSUPPORTED,"Unsupported wide instruction");
            } else if (op == 0x10 || op == 0x12 || (op >= 0x15 && op <= 0x19) ||
                       (op >= 0x36 && op <= 0x3a) || op == 0xbc) n = 2;
            else if (op == 0x11 || op == 0x13 || op == 0x14 || op == 0x84 ||
                     (op >= 0x99 && op <= 0xa7) || (op >= 0xb2 && op <= 0xb8) ||
                     op == 0xbb || op == 0xbd || op == 0xc0 || op == 0xc1 || op == 0xc6 || op == 0xc7) n = 3;
            else if (op == 0xb9 || op == 0xba || op == 0xc8) n = 5;
            else if (op == 0xc5) n = 4;
            else if (op == 0xa8 || op == 0xa9 || op >= 0xc9) return fail(v,NV_JAVA_UNSUPPORTED,"Legacy or reserved opcode");
            if (n > m->length - pc) return fail(v,NV_JAVA_FORMAT,"Truncated bytecode operand");
            if (pass && ((op >= 0x99 && op <= 0xa7) || op == 0xc6 || op == 0xc7) &&
                !branch_target(v,m,pc,signed16((u32)c[pc+1]<<8|c[pc+2]))) return false;
            if (pass && op == 0xc8 && !branch_target(v,m,pc,signed32(big32(c+pc+1)))) return false;
            pc += n;
        }
    }
    return good(v);
}
static bool parse_pool(struct vm *v, struct reader *r) {
    v->c->pool_count = read_n(r,2);
    if (!v->c->pool_count || v->c->pool_count > CP_MAX) return fail(v,NV_JAVA_LIMIT,"Constant-pool limit exceeded");
    for (u32 i = 1; i < v->c->pool_count && good(v); ++i) {
        struct cp *c = &v->c->pool[i]; c->tag = read_n(r,1);
        switch (c->tag) {
        case 1:
            c->length = read_n(r,2); c->text = r->bytes+r->at;
            if (!skip(r,c->length) || !valid_mutf(c->text,c->length)) return fail(v,NV_JAVA_FORMAT,"Invalid modified UTF-8");
            break;
        case 3: case 4: c->bits = read_n(r,4); break;
        case 5: case 6:
            c->bits = (u64)read_n(r,4)<<32; c->bits |= read_n(r,4);
            if (++i >= v->c->pool_count) return fail(v,NV_JAVA_FORMAT,"Missing wide constant-pool slot");
            break;
        case 7: case 8: case 16: case 19: case 20: c->a=read_n(r,2); break;
        case 9: case 10: case 11: case 12: case 17: case 18:
            c->a=read_n(r,2); c->b=read_n(r,2); break;
        case 15: c->a=read_n(r,1); c->b=read_n(r,2); break;
        default: return fail(v,NV_JAVA_FORMAT,"Unknown constant-pool tag");
        }
    }
    for (u32 i=1;i<v->c->pool_count && good(v);++i) {
        struct cp *c=&v->c->pool[i];
        switch(c->tag) {
        case 7: case 8: case 16: case 19: case 20: constant(v,c->a,1); break;
        case 9: case 10: case 11: constant(v,c->a,7); constant(v,c->b,12); break;
        case 12: constant(v,c->a,1); constant(v,c->b,1); break;
        case 17: case 18: constant(v,c->b,12); break;
        case 15: {
            if (c->a<1 || c->a>9 || !c->b || c->b>=v->c->pool_count) return fail(v,NV_JAVA_FORMAT,"Invalid method handle");
            u8 tag=v->c->pool[c->b].tag;
            if ((c->a<=4 && tag!=9) || (c->a==9 && tag!=11) ||
                ((c->a==5 || c->a==8) && tag!=10) || ((c->a==6 || c->a==7) && tag!=10 && tag!=11))
                return fail(v,NV_JAVA_FORMAT,"Invalid method-handle target");
            break;
        }
        default: break;
        }
    }
    return good(v);
}
static bool attributes(struct vm *v, struct reader *r, struct method *m, struct field *f, bool top) {
    u32 count=read_n(r,2); bool saw_code=false, saw_constant=false, saw_boot=false;
    for (u32 i=0;i<count && good(v);++i) {
        u16 name=read_n(r,2); u32 length=read_n(r,4);
        if (!constant(v,name,1) || length>r->size-r->at) return fail(v,NV_JAVA_FORMAT,"Invalid attribute length");
        struct reader a={r->bytes+r->at,length,0,v}; r->at+=length;
        if (m && text_is(v,name,"Code")) {
            if (saw_code) return fail(v,NV_JAVA_FORMAT,"Duplicate Code attribute");
            saw_code=true; m->stack=read_n(&a,2); m->locals=read_n(&a,2); m->length=read_n(&a,4);
            if (!m->length || m->length>CODE_MAX || m->stack>SLOT_MAX || m->locals>SLOT_MAX)
                return fail(v,NV_JAVA_LIMIT,"Method code or slot limit exceeded");
            m->code=a.bytes+a.at;
            if (!skip(&a,m->length)) return false;
            if (read_n(&a,2)) return fail(v,NV_JAVA_UNSUPPORTED,"Java exception handlers are not implemented");
            if (!attributes(v,&a,NULL,NULL,false) || !decode(v,m)) return false;
        } else if (f && text_is(v,name,"ConstantValue")) {
            if (saw_constant || length!=2) return fail(v,NV_JAVA_FORMAT,"Invalid ConstantValue attribute");
            saw_constant=true; f->constant=read_n(&a,2);
            if (!f->constant) return fail(v,NV_JAVA_FORMAT,"Invalid field constant");
        } else if (top && text_is(v,name,"BootstrapMethods")) {
            if (saw_boot) return fail(v,NV_JAVA_FORMAT,"Duplicate bootstrap table");
            saw_boot=true; v->c->boot_count=read_n(&a,2);
            if (v->c->boot_count>BOOT_MAX) return fail(v,NV_JAVA_LIMIT,"Bootstrap-method limit exceeded");
            for (u32 j=0;j<v->c->boot_count && good(v);++j) {
                struct bootstrap *b=&v->c->boots[j];
                b->handle=read_n(&a,2); b->count=read_n(&a,2);
                if (!constant(v,b->handle,15)) return false;
                if (b->count>ARG_MAX) return fail(v,NV_JAVA_LIMIT,"Bootstrap-argument limit exceeded");
                for (u32 k=0;k<b->count;++k) {
                    b->args[k]=read_n(&a,2);
                    if (!b->args[k] || b->args[k]>=v->c->pool_count || !v->c->pool[b->args[k]].tag)
                        return fail(v,NV_JAVA_FORMAT,"Invalid bootstrap argument");
                }
            }
        } else a.at=a.size; /* Length-checked metadata; runtime checks replace StackMapTable use. */
        if (a.at!=a.size) return fail(v,NV_JAVA_FORMAT,"Attribute length mismatch");
    }
    if (m && !saw_code) return fail(v,NV_JAVA_UNSUPPORTED,"Abstract/native methods are not implemented");
    return good(v);
}
static bool parse_class(struct vm *v, const void *data, u32 size) {
    struct reader r={data,size,0,v};
    if (read_n(&r,4)!=0xcafebabeu) return fail(v,NV_JAVA_FORMAT,"Expected a .class file (JAR loading is not implemented)");
    u32 minor=read_n(&r,2); v->c->major=read_n(&r,2);
    if (minor || v->c->major<52 || v->c->major>65)
        return fail(v,NV_JAVA_UNSUPPORTED,"Expected non-preview Java 8 through Java 21 class format");
    if (!parse_pool(v,&r)) return false;
    u16 flags=read_n(&r,2); v->c->flags=flags;
    v->c->self=read_n(&r,2); u16 parent=read_n(&r,2);
    if (!constant(v,v->c->self,7) || !class_is(v,parent,"java/lang/Object"))
        return fail(v,NV_JAVA_UNSUPPORTED,"Application classes must directly extend Object");
    struct cp *self_name=constant(v,v->c->pool[v->c->self].a,1);
    if (!self_name || !valid_class_name(self_name->text,self_name->length))
        return fail(v,NV_JAVA_FORMAT,"Invalid or unsafe class name");
    if (self_name->length>=5 && !memcmp(self_name->text,"java/",5))
        return fail(v,NV_JAVA_UNSUPPORTED,"Applications cannot replace bootstrap java classes");
    if (flags&~0x1031u) return fail(v,NV_JAVA_UNSUPPORTED,"Interfaces, enums, annotations and modules are not implemented");
    if (read_n(&r,2)) return fail(v,NV_JAVA_UNSUPPORTED,"Interfaces are not implemented");
    v->c->field_count=read_n(&r,2);
    if (v->c->field_count>FIELD_MAX) return fail(v,NV_JAVA_LIMIT,"Field limit exceeded");
    for (u32 i=0;i<v->c->field_count && good(v);++i) {
        struct field *f=&v->c->fields[i];
        f->flags=read_n(&r,2); f->name=read_n(&r,2); f->desc=read_n(&r,2);
        if (!constant(v,f->name,1)) return false;
        f->value.tag=field_type(v,f->desc);
        if (!good(v) || !attributes(v,&r,NULL,f,false)) return false;
        for (u32 j=0;j<i;++j) if (text_same(v,f->name,v->c->fields[j].name) && text_same(v,f->desc,v->c->fields[j].desc))
            return fail(v,NV_JAVA_FORMAT,"Duplicate field");
    }
    v->c->method_count=read_n(&r,2);
    if (v->c->method_count>METHOD_MAX) return fail(v,NV_JAVA_LIMIT,"Method limit exceeded");
    for (u32 i=0;i<v->c->method_count && good(v);++i) {
        struct method *m=&v->c->methods[i]; struct signature sig;
        m->flags=read_n(&r,2); m->name=read_n(&r,2); m->desc=read_n(&r,2);
        if (!constant(v,m->name,1) || !signature(v,m->desc,&sig)) return false;
        if (m->flags&(0x20|0x100|0x400)) return fail(v,NV_JAVA_UNSUPPORTED,"Synchronized/native/abstract methods are not implemented");
        if (!attributes(v,&r,m,NULL,false)) return false;
        if (sig.slots+(!(m->flags&8))>m->locals) return fail(v,NV_JAVA_VERIFY,"Arguments exceed method locals");
        if (text_is(v,m->name,"<init>") && ((m->flags&8) || sig.result!=T_VOID))
            return fail(v,NV_JAVA_FORMAT,"Invalid constructor");
        if (text_is(v,m->name,"<clinit>") && (!(m->flags&8) || !text_is(v,m->desc,"()V")))
            return fail(v,NV_JAVA_FORMAT,"Invalid class initializer");
        for (u32 j=0;j<i;++j) if (text_same(v,m->name,v->c->methods[j].name) && text_same(v,m->desc,v->c->methods[j].desc))
            return fail(v,NV_JAVA_FORMAT,"Duplicate method");
    }
    if (!attributes(v,&r,NULL,NULL,true)) return false;
    for (u32 i=1;i<v->c->pool_count;++i) if ((v->c->pool[i].tag==17 || v->c->pool[i].tag==18) && v->c->pool[i].a>=v->c->boot_count)
        return fail(v,NV_JAVA_FORMAT,"Invalid bootstrap index");
    return good(v) && r.at==r.size ? true : fail(v,NV_JAVA_FORMAT,"Trailing class-file bytes");
}

static struct value integer(u32 n) { return (struct value){n,T_INT}; }
static struct value wide(u64 n) { return (struct value){n,T_LONG}; }
static struct value reference(u32 n) { return (struct value){n,T_REF}; }
static struct value empty(void) { return (struct value){0,T_EMPTY}; }
static u32 new_object(struct vm *v, u8 kind, u8 cls, u32 length, u32 bytes) {
    if (v->object_count==OBJECT_MAX-1) { fail(v,NV_JAVA_MEMORY,"Java object limit exceeded"); return 0; }
    if (bytes>v->capacity-sizeof(struct object)) { fail(v,NV_JAVA_MEMORY,"Java allocation exceeds arena"); return 0; }
    struct object *o=allocate(v,sizeof(*o)+bytes);
    if (!o) return 0;
    o->kind=kind; o->cls=cls; o->length=length;
    v->objects[++v->object_count]=o;
    return v->object_count;
}
static struct object *object(struct vm *v, struct value value) {
    if (value.tag!=T_REF || value.bits>v->object_count) { fail(v,NV_JAVA_VERIFY,"Invalid object reference"); return NULL; }
    if (!value.bits) { fail(v,NV_JAVA_EXCEPTION,"NullPointerException"); return NULL; }
    return v->objects[value.bits];
}
static u32 string_units(struct vm *v, const u16 *units, u32 length) {
    if (length>STRING_MAX) { fail(v,NV_JAVA_LIMIT,"String length limit exceeded"); return 0; }
    u32 id=new_object(v,O_STRING,C_STRING,length,length*2);
    if (id && length) memcpy(v->objects[id]->data,units,length*2);
    return id;
}
static u32 string_constant(struct vm *v, u16 index) {
    struct cp *c=constant(v,index,8);
    if (!c) return 0;
    if (v->c->strings[index]) return v->c->strings[index];
    struct cp *utf=constant(v,c->a,1);
    if (!utf) return 0;
    for (u32 ci=0;ci<v->class_count;++ci) {
        struct java_class *other=v->classes[ci];
        for (u32 i=1;i<other->pool_count;++i) if (other->strings[i]) {
            struct cp *text=&other->pool[other->pool[i].a];
            if (text->length==utf->length && !memcmp(text->text,utf->text,utf->length))
                return v->c->strings[index]=other->strings[i];
        }
    }
    u32 at=0,count=0; u16 unit;
    while (at<utf->length) { if (!mutf_unit(utf->text,utf->length,&at,&unit)) return 0; ++count; }
    if (count>STRING_MAX) { fail(v,NV_JAVA_LIMIT,"String constant exceeds limit"); return 0; }
    u32 id=new_object(v,O_STRING,C_STRING,count,count*2);
    if (!id) return 0;
    at=0; u16 *units=(u16 *)v->objects[id]->data;
    for (u32 i=0;i<count;++i) mutf_unit(utf->text,utf->length,&at,&units[i]);
    v->c->strings[index]=id; return id;
}
static u32 argument_string(struct vm *v, const char *text) {
    u16 *units=v->text_buffer; u32 at=0,count=0;
    usize length=strnlen(text,STRING_MAX+1);
    if (length>STRING_MAX) { fail(v,NV_JAVA_LIMIT,"Argument string too long"); return 0; }
    const u8 *s=(const u8 *)text;
    while (at<length) {
        u32 c=s[at++], extra=0, minimum=0;
        if (c>=0xc2 && c<=0xdf) { c&=31; extra=1; minimum=128; }
        else if (c>=0xe0 && c<=0xef) { c&=15; extra=2; minimum=2048; }
        else if (c>=0xf0 && c<=0xf4) { c&=7; extra=3; minimum=65536; }
        else if (c>=128) { fail(v,NV_JAVA_FORMAT,"Argument is not UTF-8"); return 0; }
        if (extra>length-at) { fail(v,NV_JAVA_FORMAT,"Truncated UTF-8 argument"); return 0; }
        while (extra--) {
            u32 next=s[at++];
            if ((next&0xc0)!=0x80) { fail(v,NV_JAVA_FORMAT,"Invalid UTF-8 argument"); return 0; }
            c=(c<<6)|(next&63);
        }
        if (c<minimum || c>0x10ffff || (c>=0xd800 && c<=0xdfff)) { fail(v,NV_JAVA_FORMAT,"Invalid UTF-8 scalar"); return 0; }
        if (c>65535) { c-=65536; units[count++]=0xd800+(c>>10); units[count++]=0xdc00+(c&1023); }
        else units[count++]=c;
    }
    return string_units(v,units,count);
}
static bool push(struct vm *v, struct frame *f, struct value value) {
    u32 slots=value.tag==T_LONG?2:1;
    if (value.tag!=T_INT && value.tag!=T_LONG && value.tag!=T_REF) return fail(v,NV_JAVA_VERIFY,"Invalid operand type");
    if (slots>(u32)(v->classes[f->owner]->methods[f->method].stack-f->used)) return fail(v,NV_JAVA_VERIFY,"Operand stack overflow");
    f->stack[f->used++]=value;
    if (slots==2) f->stack[f->used++]=(struct value){0,T_TOP};
    return true;
}
static struct value pop(struct vm *v, struct frame *f, u8 expected) {
    if (!f->used) { fail(v,NV_JAVA_VERIFY,"Operand stack underflow"); return empty(); }
    struct value value=f->stack[--f->used];
    if (value.tag==T_TOP) {
        if (!f->used || f->stack[f->used-1].tag!=T_LONG) { fail(v,NV_JAVA_VERIFY,"Broken wide operand"); return empty(); }
        value=f->stack[--f->used];
    } else if (value.tag==T_LONG) { fail(v,NV_JAVA_VERIFY,"Missing wide operand slot"); return empty(); }
    if (expected && value.tag!=expected) fail(v,NV_JAVA_VERIFY,"Operand type mismatch");
    return value;
}
static bool local_store(struct vm *v, struct frame *f, u32 index, struct value value) {
    u32 slots=value.tag==T_LONG?2:1, limit=v->classes[f->owner]->methods[f->method].locals;
    if (index>=limit || slots>limit-index) return fail(v,NV_JAVA_VERIFY,"Local index out of range");
    for (u32 j=index;j<index+slots;++j) {
        if (f->locals[j].tag==T_TOP && j) f->locals[j-1]=empty();
        if (f->locals[j].tag==T_LONG && j+1<limit) f->locals[j+1]=empty();
    }
    f->locals[index]=value;
    if (slots==2) f->locals[index+1]=(struct value){0,T_TOP};
    return good(v);
}
static bool local_load(struct vm *v, struct frame *f, u32 index, u8 tag) {
    u32 limit=v->classes[f->owner]->methods[f->method].locals;
    if (index>=limit || f->locals[index].tag!=tag ||
        (tag==T_LONG && (index+1>=limit || f->locals[index+1].tag!=T_TOP)))
        return fail(v,NV_JAVA_VERIFY,"Uninitialized or mistyped local");
    return push(v,f,f->locals[index]);
}
static bool increment(struct vm *v,struct frame *f,u32 index,i64 delta) {
    if (index>=v->classes[f->owner]->methods[f->method].locals || f->locals[index].tag!=T_INT)
        return fail(v,NV_JAVA_VERIFY,"iinc requires an initialized int local");
    f->locals[index].bits=(u32)(f->locals[index].bits+delta);
    return true;
}
static bool enter(struct vm *v, u16 method, const struct value *args, u32 count) {
    if (v->depth==FRAME_MAX) return fail(v,NV_JAVA_LIMIT,"Java call-depth limit exceeded");
    struct frame *f=&v->frames[v->depth++]; memset(f,0,sizeof(*f)); f->method=method; f->owner=v->c->id;
    u32 at=0;
    for (u32 i=0;i<count && good(v);++i) { local_store(v,f,at,args[i]); at+=args[i].tag==T_LONG?2:1; }
    return good(v);
}
static int method_named(struct vm *v, const char *name, const char *desc) {
    for (u32 i=0;i<v->c->method_count;++i)
        if (text_is(v,v->c->methods[i].name,name) && text_is(v,v->c->methods[i].desc,desc)) return i;
    return -1;
}
static bool append_unit(struct vm *v, u16 *out, u32 *length, u16 c) {
    if (*length==STRING_MAX) return fail(v,NV_JAVA_LIMIT,"Formatted string exceeds limit");
    out[(*length)++]=c; return true;
}
static bool append_ascii(struct vm *v,u16 *out,u32 *length,const char *s) {
    while (*s && good(v)) append_unit(v,out,length,(u8)*s++);
    return good(v);
}
static bool append_value(struct vm *v,u16 *out,u32 *length,struct value val,u8 type) {
    if (val.tag==T_REF) {
        if (!val.bits) return append_ascii(v,out,length,"null");
        struct object *o=object(v,val);
        if (!o || o->kind!=O_STRING) return fail(v,NV_JAVA_UNSUPPORTED,"Object.toString is not implemented");
        const u16 *s=(const u16 *)o->data;
        for (u32 i=0;i<o->length && good(v);++i) append_unit(v,out,length,s[i]);
    } else if (val.tag==T_INT && type=='Z') return append_ascii(v,out,length,val.bits?"true":"false");
    else if (val.tag==T_INT && type=='C') return append_unit(v,out,length,(u16)val.bits);
    else if (val.tag==T_INT || val.tag==T_LONG) {
        u64 bits=val.bits, magnitude; bool negative;
        if (val.tag==T_INT) { bits=(u32)bits; negative=(bits>>31)!=0; magnitude=negative?(u32)(0u-(u32)bits):bits; }
        else { negative=(bits>>63)!=0; magnitude=negative?0ull-bits:bits; }
        if (negative) append_unit(v,out,length,'-');
        char digits[20]; u32 n=0;
        do { digits[n++]=(char)('0'+magnitude%10); magnitude/=10; } while (magnitude);
        while (n && good(v)) append_unit(v,out,length,digits[--n]);
    } else return fail(v,NV_JAVA_VERIFY,"Cannot format operand");
    return good(v);
}
static bool output_units(struct vm *v, const u16 *units, u32 length, bool newline) {
    char bytes[256]; u32 used=0;
    for (u32 i=0;i<length;++i) {
        u32 c=units[i];
        if (c>=0xd800 && c<=0xdbff && i+1<length && units[i+1]>=0xdc00 && units[i+1]<=0xdfff)
            c=0x10000+((c-0xd800)<<10)+(units[++i]-0xdc00);
        else if (c>=0xd800 && c<=0xdfff) c=0xfffd;
        if (used>sizeof(bytes)-4) {
            if (v->host.write(v->host.context,bytes,used)) return fail(v,NV_JAVA_IO,"Console write failed");
            used=0;
        }
        if (c<128) bytes[used++]=c;
        else if (c<2048) { bytes[used++]=0xc0|(c>>6); bytes[used++]=0x80|(c&63); }
        else if (c<65536) { bytes[used++]=0xe0|(c>>12); bytes[used++]=0x80|((c>>6)&63); bytes[used++]=0x80|(c&63); }
        else { bytes[used++]=0xf0|(c>>18); bytes[used++]=0x80|((c>>12)&63); bytes[used++]=0x80|((c>>6)&63); bytes[used++]=0x80|(c&63); }
    }
    if (used && v->host.write(v->host.context,bytes,used)) return fail(v,NV_JAVA_IO,"Console write failed");
    if (newline && v->host.write(v->host.context,"\n",1)) return fail(v,NV_JAVA_IO,"Console write failed");
    return true;
}
static bool native_call(struct vm *v, struct frame *f, u16 cls, u16 name, u16 desc,
                        bool is_static, const struct value *args, u32 count) {
    if (!is_static && class_is(v,cls,"java/lang/Object") && text_is(v,name,"<init>") && text_is(v,desc,"()V")) {
        struct object *o=object(v,args[0]);
        return o && o->kind==O_INSTANCE ? true : fail(v,NV_JAVA_VERIFY,"Invalid constructor receiver");
    }
    if (!is_static && class_is(v,cls,"java/io/PrintStream") &&
        (text_is(v,name,"println") || text_is(v,name,"print"))) {
        struct object *o=object(v,args[0]);
        if (!o || o->kind!=O_PRINT) return fail(v,NV_JAVA_VERIFY,"Invalid PrintStream receiver");
        u16 *out=v->text_buffer; u32 length=0; u8 type=0;
        if (text_is(v,desc,"(I)V")) type='I';
        else if (text_is(v,desc,"(J)V")) type='J';
        else if (text_is(v,desc,"(Z)V")) type='Z';
        else if (text_is(v,desc,"(C)V")) type='C';
        else if (text_is(v,desc,"(Ljava/lang/String;)V")) type='L';
        else if (!text_is(v,desc,"()V") || !text_is(v,name,"println"))
            return fail(v,NV_JAVA_UNSUPPORTED,"PrintStream overload is not implemented");
        if (type && (count!=2 || !append_value(v,out,&length,args[1],type))) return false;
        return output_units(v,out,length,text_is(v,name,"println"));
    }
    if (!is_static && class_is(v,cls,"java/lang/String")) {
        struct object *o=object(v,args[0]);
        if (!o || o->kind!=O_STRING) return fail(v,NV_JAVA_VERIFY,"Invalid String receiver");
        if (text_is(v,name,"length") && text_is(v,desc,"()I")) return push(v,f,integer(o->length));
        if (text_is(v,name,"charAt") && text_is(v,desc,"(I)C")) {
            if (args[1].bits>=o->length) return fail(v,NV_JAVA_EXCEPTION,"StringIndexOutOfBoundsException");
            return push(v,f,integer(((u16 *)o->data)[args[1].bits]));
        }
        if (text_is(v,name,"equals") && text_is(v,desc,"(Ljava/lang/Object;)Z")) {
            bool equal=false;
            if (args[1].bits) {
                struct object *other=object(v,args[1]);
                if (!other) return false;
                equal=other->kind==O_STRING && o->length==other->length && !memcmp(o->data,other->data,o->length*2);
            }
            return push(v,f,integer(equal));
        }
    }
    return fail(v,NV_JAVA_UNSUPPORTED,"Java library method is not implemented");
}
static bool concat(struct vm *v, struct frame *f, u16 index) {
    struct cp *dyn=constant(v,index,18);
    if (!dyn) return false;
    struct cp *nt=constant(v,dyn->b,12);
    if (!nt || dyn->a>=v->c->boot_count) return fail(v,NV_JAVA_FORMAT,"Invalid invokedynamic bootstrap");
    struct bootstrap *b=&v->c->boots[dyn->a];
    struct cp *handle=constant(v,b->handle,15);
    if (!handle || handle->a!=6) return fail(v,NV_JAVA_UNSUPPORTED,"Only static string concatenation bootstrap is implemented");
    struct cp *ref=constant(v,handle->b,10);
    struct cp *target=ref?constant(v,ref->b,12):NULL;
    if (!target || !class_is(v,ref->a,"java/lang/invoke/StringConcatFactory") ||
        !text_is(v,target->a,"makeConcatWithConstants") || !text_is(v,target->b,
        "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/invoke/MethodType;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/invoke/CallSite;") || !b->count)
        return fail(v,NV_JAVA_UNSUPPORTED,"invokedynamic target is not supported (lambdas are not implemented)");
    struct signature sig;
    if (!signature(v,nt->b,&sig) || sig.result!=T_REF) return fail(v,NV_JAVA_VERIFY,"Invalid concat signature");
    struct cp *desc=constant(v,nt->b,1); u32 at=1; u8 types[ARG_MAX];
    for (u32 i=0;i<sig.count;++i) { types[i]=desc->text[at]; descriptor_type(v,desc->text,desc->length,&at,false); }
    if (desc->length-at!=19 || memcmp(desc->text+at,")Ljava/lang/String;",19))
        return fail(v,NV_JAVA_VERIFY,"Concat must return String");
    struct value args[ARG_MAX];
    for (u32 i=sig.count;i && good(v);--i) args[i-1]=pop(v,f,sig.args[i-1]);
    u32 recipe_id=string_constant(v,b->args[0]);
    if (!recipe_id || !good(v)) return false;
    struct object *recipe=v->objects[recipe_id];
    const u16 *units=(const u16 *)recipe->data;
    u16 *out=v->text_buffer; u32 length=0,arg=0,literal=1;
    for (u32 i=0;i<recipe->length && good(v);++i) {
        if (units[i]==1) {
            if (arg>=sig.count) return fail(v,NV_JAVA_FORMAT,"Concat recipe has too many arguments");
            append_value(v,out,&length,args[arg],types[arg]); ++arg;
        } else if (units[i]==2) {
            if (literal>=b->count) return fail(v,NV_JAVA_FORMAT,"Concat recipe has too many constants");
            u16 ci=b->args[literal++]; struct cp *c=&v->c->pool[ci];
            if (c->tag==8) append_value(v,out,&length,reference(string_constant(v,ci)),'L');
            else if (c->tag==3) append_value(v,out,&length,integer(c->bits),'I');
            else if (c->tag==5) append_value(v,out,&length,wide(c->bits),'J');
            else return fail(v,NV_JAVA_UNSUPPORTED,"Unsupported concat constant");
        } else append_unit(v,out,&length,units[i]);
    }
    if (arg!=sig.count || literal!=b->count) return fail(v,NV_JAVA_FORMAT,"Concat recipe argument count mismatch");
    u32 id=good(v)?string_units(v,out,length):0;
    return id && push(v,f,reference(id));
}

static struct cp *class_name(struct java_class *c) { return &c->pool[c->pool[c->self].a]; }
static bool equal_text(const struct cp *a,const struct cp *b) {
    return a->length==b->length && !memcmp(a->text,b->text,a->length);
}
static bool prepare_fields(struct vm *v) {
    for (u32 i=0;i<v->c->field_count && good(v);++i) {
        struct field *f=&v->c->fields[i];
        if (!f->constant) continue;
        u8 tag=f->value.tag==T_INT?3:f->value.tag==T_LONG?5:8;
        struct cp *c=constant(v,f->constant,tag);
        if (!c) return false;
        if (tag==8 && !text_is(v,f->desc,"Ljava/lang/String;"))
            return fail(v,NV_JAVA_FORMAT,"Invalid reference ConstantValue");
        if (f->flags&8) {
            f->value.bits=tag==8?string_constant(v,f->constant):c->bits;
            u8 type=v->c->pool[f->desc].text[0];
            if (type=='B') f->value.bits=(u32)signed8(f->value.bits&255);
            if (type=='S') f->value.bits=(u32)signed16(f->value.bits&65535);
            if (type=='C') f->value.bits&=65535;
            if (type=='Z') f->value.bits&=1;
        }
    }
    return good(v);
}
/* Loading does not initialize classes or recurse through symbolic references.
 * A class's bytes and metadata share the same bounded process arena. */
static struct java_class *load_class(struct vm *v,const u8 *name,u32 n) {
    if (!valid_class_name(name,n)) { fail(v,NV_JAVA_FORMAT,"Invalid or unsafe class name"); return NULL; }
    for (u32 i=0;i<v->class_count;++i) {
        struct cp *own=class_name(v->classes[i]);
        if (own->length==n && !memcmp(own->text,name,n)) return v->classes[i];
    }
    if (n>=5 && !memcmp(name,"java/",5)) {
        fail(v,NV_JAVA_UNSUPPORTED,"Referenced bootstrap class is not implemented"); return NULL;
    }
    if (!v->host.read_class) { fail(v,NV_JAVA_UNSUPPORTED,"No classpath loader for referenced class"); return NULL; }
    if (v->class_count==NV_JAVA_CLASSES) { fail(v,NV_JAVA_LIMIT,"Java loaded-class limit exceeded"); return NULL; }
    char path[NV_JAVA_NAME_MAX]; memcpy(path,name,n); path[n]=0;
    u32 length=0;
    if (v->host.read_class(v->host.context,path,NULL,0,&length)) {
        fail(v,NV_JAVA_IO,"Class not found or unreadable in classpath"); return NULL;
    }
    if (length<10 || length>NV_JAVA_CLASS_MAX) { fail(v,NV_JAVA_FORMAT,"Invalid dependency class size"); return NULL; }
    u8 *bytes=allocate(v,length);
    struct java_class *c=allocate(v,sizeof(*c));
    if (!bytes || !c) return NULL;
    u32 actual=0;
    if (v->host.read_class(v->host.context,path,bytes,length,&actual) || actual!=length) {
        fail(v,NV_JAVA_IO,"Dependency class read changed or was incomplete"); return NULL;
    }
    struct java_class *saved=v->c; v->c=c;
    bool parsed=parse_class(v,bytes,length);
    if (parsed) {
        struct cp *own=class_name(c);
        if (own->length!=n || memcmp(own->text,name,n)) parsed=fail(v,NV_JAVA_VERIFY,"Class name does not match requested dependency");
    }
    if (parsed) {
        c->id=v->class_count; v->classes[v->class_count++]=c;
        parsed=prepare_fields(v);
    }
    v->c=saved;
    return parsed?c:NULL;
}
static bool member_accessible(struct vm *v,struct java_class *owner,u16 flags);
static struct java_class *resolve_class(struct vm *v,u16 index) {
    struct cp *cls=constant(v,index,7), *name=cls?constant(v,cls->a,1):NULL;
    struct java_class *c=name?load_class(v,name->text,name->length):NULL;
    return c && member_accessible(v,c,c->flags)?c:NULL;
}
/* Return true if a new Java frame was scheduled. The caller retries its
 * instruction with operands untouched after <clinit> returns. Cycles observe
 * prepared defaults, exactly once on this single Java thread. */
static bool initialize(struct vm *v,struct java_class *c,struct frame *caller,u32 pc) {
    if (c->state) return false;
    c->state=1;
    struct java_class *saved=v->c; v->c=c;
    int method=method_named(v,"<clinit>","()V");
    if (method>=0) {
        if (caller) caller->pc=pc;
        if (enter(v,method,NULL,0)) v->frames[v->depth-1].initializer=true;
    } else c->state=2;
    v->c=saved;
    return method>=0;
}
static bool member_accessible(struct vm *v,struct java_class *owner,u16 flags) {
    if (owner==v->c || (flags&1)) return true;
    if (flags&2) return fail(v,NV_JAVA_VERIFY,"Private cross-class access (nestmates not implemented)");
    struct cp *a=class_name(owner),*b=class_name(v->c); u32 ap=0,bp=0;
    for (u32 i=0;i<a->length;++i) if (a->text[i]=='/') ap=i+1;
    for (u32 i=0;i<b->length;++i) if (b->text[i]=='/') bp=i+1;
    return ap==bp && !memcmp(a->text,b->text,ap)?true:fail(v,NV_JAVA_VERIFY,"Member is not accessible from this package");
}
static int named_type(struct vm *v,const u8 *name,u32 n) {
    static const char *const names[]={"java/lang/Object",NULL,"java/lang/String","java/io/PrintStream",
        "[I","[J","[B","[C","[S","[Z","[Ljava/lang/Object;",NULL,"[Ljava/lang/String;"};
    for (u32 i=0;i<ARRAY_LEN(names);++i)
        if (names[i] && n==strlen(names[i]) && !memcmp(name,names[i],n)) return i;
    bool array=n>3 && name[0]=='[' && name[1]=='L' && name[n-1]==';';
    if (n && name[0]=='[' && !array) {
        fail(v,NV_JAVA_UNSUPPORTED,"Array class type is not implemented"); return -1;
    }
    struct java_class *c=load_class(v,name+(array?2:0),n-(array?3:0));
    return c?(array?C_APP_ARRAY:C_APP)+c->id:-1;
}
static int class_id(struct vm *v,u16 index) {
    struct cp *c=constant(v,index,7), *name=c?constant(v,c->a,1):NULL;
    return name?named_type(v,name->text,name->length):-1;
}
static bool app_array(int cls) { return cls>=C_APP_ARRAY && cls<C_APP_ARRAY+(int)NV_JAVA_CLASSES; }
static bool app_instance(int cls) { return cls>=C_APP && cls<C_APP+(int)NV_JAVA_CLASSES; }
static bool instance_of(struct object *o,int cls) {
    return cls==C_OBJECT || o->cls==cls ||
           (cls==C_OBJECTS && (app_array(o->cls) || o->cls==C_STRINGS));
}
static bool reference_fits(struct vm *v,struct value val,u16 desc,u32 at) {
    if (val.tag!=T_REF || !val.bits) return true;
    struct object *o=object(v,val); struct cp *d=constant(v,desc,1);
    if (!o || !d || at>=d->length) return fail(v,NV_JAVA_VERIFY,"Invalid reference descriptor");
    u32 end=at; descriptor_type(v,d->text,d->length,&end,false);
    if (!good(v)) return false;
    const u8 *s=d->text+at; u32 n=end-at;
    bool wrapped=s[0]=='L';
    int cls=named_type(v,s+(wrapped?1:0),n-(wrapped?2:0));
    if (!good(v)) return false;
    return instance_of(o,cls)?true:fail(v,NV_JAVA_VERIFY,"Reference does not match declared type");
}
static bool array_store_compatible(struct vm *v,struct object *array,struct value val) {
    if (!val.bits) return true;
    struct object *o=object(v,val);
    int cls=array->cls==C_OBJECTS?C_OBJECT:app_array(array->cls)?C_APP+array->cls-C_APP_ARRAY:C_STRING;
    return o && instance_of(o,cls) ? true : fail(v,NV_JAVA_EXCEPTION,"ArrayStoreException");
}
static bool field_access(struct vm *v,struct frame *f,u8 op,u16 index,u32 pc) {
    struct cp *ref=constant(v,index,9);
    struct cp *nt=ref?constant(v,ref->b,12):NULL;
    if (!nt) return false;
    bool is_static=op==0xb2 || op==0xb3, store=op==0xb3 || op==0xb5;
    if (op==0xb2 && class_is(v,ref->a,"java/lang/System") && text_is(v,nt->a,"out") &&
        text_is(v,nt->b,"Ljava/io/PrintStream;")) return push(v,f,reference(1));
    struct java_class *owner=resolve_class(v,ref->a);
    if (!owner) return false;
    for (u32 i=0;i<owner->field_count;++i) {
        struct field *field=&owner->fields[i];
        if (!equal_text(&owner->pool[field->name],&v->c->pool[nt->a]) ||
            !equal_text(&owner->pool[field->desc],&v->c->pool[nt->b])) continue;
        if (!!(field->flags&8)!=is_static) return fail(v,NV_JAVA_VERIFY,"Field static/instance mismatch");
        if (!member_accessible(v,owner,field->flags)) return false;
        if (store && (field->flags&16) && (owner!=v->c ||
            !text_is(v,v->c->methods[f->method].name,is_static?"<clinit>":"<init>")))
            return fail(v,NV_JAVA_VERIFY,"Final field write outside declaring initializer");
        if (is_static && initialize(v,owner,f,pc)) return good(v);
        struct value value=store?pop(v,f,field->value.tag):field->value;
        struct object *o=NULL;
        if (!is_static) {
            o=object(v,pop(v,f,T_REF));
            if (!o || o->kind!=O_INSTANCE || o->cls!=C_APP+owner->id) return fail(v,NV_JAVA_VERIFY,"Invalid field receiver");
            if (!store) value.bits=o->data[i];
        }
        if (!good(v)) return false;
        if (store) {
            if (!reference_fits(v,value,nt->b,0)) return false;
            struct cp *desc=constant(v,nt->b,1);
            if (!desc) return false;
            if (desc->text[0]=='B') value.bits=(u32)signed8(value.bits&255);
            if (desc->text[0]=='S') value.bits=(u32)signed16(value.bits&65535);
            if (desc->text[0]=='C') value.bits&=65535;
            if (desc->text[0]=='Z') value.bits&=1;
            if (is_static) field->value=value; else o->data[i]=value.bits;
            return true;
        }
        return push(v,f,value);
    }
    return fail(v,NV_JAVA_VERIFY,"Field not found");
}
static bool invoke(struct vm *v,struct frame *f,u8 op,u16 index,u32 pc) {
    struct cp *ref=constant(v,index,10);
    struct cp *nt=ref?constant(v,ref->b,12):NULL;
    struct signature sig;
    if (!nt || !signature(v,nt->b,&sig)) return false;
    bool is_static=op==0xb8;
    struct java_class *owner=NULL; int method=-1;
    struct cp *target_name=constant(v,v->c->pool[ref->a].a,1);
    if (!target_name) return false;
    if (target_name->length<5 || memcmp(target_name->text,"java/",5)) {
        owner=resolve_class(v,ref->a);
        if (!owner) return false;
        for (u32 i=0;i<owner->method_count;++i) {
            struct method *m=&owner->methods[i];
            if (!equal_text(&owner->pool[m->name],&v->c->pool[nt->a]) ||
                !equal_text(&owner->pool[m->desc],&v->c->pool[nt->b])) continue;
            if (!!(m->flags&8)!=is_static) return fail(v,NV_JAVA_VERIFY,"Method static/instance mismatch");
            if (!member_accessible(v,owner,m->flags)) return false;
            if (text_is(v,nt->a,"<clinit>") || (text_is(v,nt->a,"<init>") && op!=0xb7))
                return fail(v,NV_JAVA_VERIFY,"Invalid explicit initializer invocation");
            method=i; break;
        }
        if (method<0) return fail(v,NV_JAVA_VERIFY,"Method not found");
        if (is_static && initialize(v,owner,f,pc)) return good(v);
    }
    struct value args[ARG_MAX+1]; u32 base=is_static?0:1;
    for (u32 i=sig.count;i && good(v);--i) args[base+i-1]=pop(v,f,sig.args[i-1]);
    if (!is_static) args[0]=pop(v,f,T_REF);
    if (!good(v)) return false;
    for (u32 i=0;i<sig.count;++i)
        if (!reference_fits(v,args[base+i],nt->b,sig.positions[i])) return false;
    if (!is_static && !object(v,args[0])) return false;
    if (owner) {
        if (!is_static && v->objects[args[0].bits]->cls!=C_APP+owner->id) return fail(v,NV_JAVA_VERIFY,"Wrong method receiver");
        v->c=owner;
        return enter(v,method,args,base+sig.count);
    }
    if (text_is(v,nt->a,"<init>") && op!=0xb7) return fail(v,NV_JAVA_VERIFY,"Invalid constructor opcode");
    return native_call(v,f,ref->a,nt->a,nt->b,is_static,args,base+sig.count);
}
static bool stack_op(struct vm *v,struct frame *f,u8 op) {
    struct value a=pop(v,f,0),b=empty(),c=empty(),d=empty();
    if (!good(v)) return false;
    bool aw=a.tag==T_LONG;
    if (op==0x57) return !aw?true:fail(v,NV_JAVA_VERIFY,"pop requires category 1");
    if (op==0x58) {
        if (!aw) { b=pop(v,f,0); if (b.tag==T_LONG) return fail(v,NV_JAVA_VERIFY,"Invalid pop2 shape"); }
        return good(v);
    }
    if (op==0x59) return !aw?(push(v,f,a)&&push(v,f,a)):fail(v,NV_JAVA_VERIFY,"Invalid dup shape");
    if (op==0x5c && aw) return push(v,f,a)&&push(v,f,a);
    b=pop(v,f,0);
    if (!good(v)) return false;
    bool bw=b.tag==T_LONG;
    if (op==0x5f) return !aw&&!bw?(push(v,f,a)&&push(v,f,b)):fail(v,NV_JAVA_VERIFY,"Invalid swap shape");
    if (op==0x5a) return !aw&&!bw?(push(v,f,a)&&push(v,f,b)&&push(v,f,a)):fail(v,NV_JAVA_VERIFY,"Invalid dup_x1 shape");
    if (op==0x5c) return !aw&&!bw?(push(v,f,b)&&push(v,f,a)&&push(v,f,b)&&push(v,f,a)):fail(v,NV_JAVA_VERIFY,"Invalid dup2 shape");
    if (op==0x5b) {
        if (aw) return fail(v,NV_JAVA_VERIFY,"Invalid dup_x2 shape");
        if (bw) return push(v,f,a)&&push(v,f,b)&&push(v,f,a);
        c=pop(v,f,0);
        return c.tag!=T_LONG && good(v)?(push(v,f,a)&&push(v,f,c)&&push(v,f,b)&&push(v,f,a)):
            fail(v,NV_JAVA_VERIFY,"Invalid dup_x2 shape");
    }
    if (op==0x5d) {
        if (bw) return fail(v,NV_JAVA_VERIFY,"Invalid dup2_x1 shape");
        if (aw) return push(v,f,a)&&push(v,f,b)&&push(v,f,a);
        c=pop(v,f,0);
        return c.tag!=T_LONG && good(v)?(push(v,f,b)&&push(v,f,a)&&push(v,f,c)&&push(v,f,b)&&push(v,f,a)):
            fail(v,NV_JAVA_VERIFY,"Invalid dup2_x1 shape");
    }
    if (op==0x5e) {
        if (aw && bw) return push(v,f,a)&&push(v,f,b)&&push(v,f,a);
        if (!aw && bw) return fail(v,NV_JAVA_VERIFY,"Invalid dup2_x2 shape");
        c=pop(v,f,0);
        if (!good(v)) return false;
        if (aw) return c.tag!=T_LONG?(push(v,f,a)&&push(v,f,c)&&push(v,f,b)&&push(v,f,a)):
            fail(v,NV_JAVA_VERIFY,"Invalid dup2_x2 shape");
        if (c.tag==T_LONG) return push(v,f,b)&&push(v,f,a)&&push(v,f,c)&&push(v,f,b)&&push(v,f,a);
        d=pop(v,f,0);
        return d.tag!=T_LONG && good(v)?(push(v,f,b)&&push(v,f,a)&&push(v,f,d)&&push(v,f,c)&&push(v,f,b)&&push(v,f,a)):
            fail(v,NV_JAVA_VERIFY,"Invalid dup2_x2 shape");
    }
    return fail(v,NV_JAVA_UNSUPPORTED,"Stack instruction is not implemented");
}
static bool arithmetic(struct vm *v,struct frame *f,u8 op) {
    if (op>=0x74 && op<=0x77) {
        if (op>0x75) return fail(v,NV_JAVA_UNSUPPORTED,"Floating-point opcode is not implemented");
        struct value a=pop(v,f,op==0x74?T_INT:T_LONG); a.bits=0ull-a.bits;
        if (a.tag==T_INT) a.bits=(u32)a.bits;
        return good(v) && push(v,f,a);
    }
    bool is_long;
    u32 operation;
    if (op<=0x73) {
        u32 kind=(op-0x60)%4;
        if (kind>1) return fail(v,NV_JAVA_UNSUPPORTED,"Floating-point opcode is not implemented");
        is_long=kind==1; operation=(op-0x60)/4;
    } else { is_long=(op&1)!=0; operation=5+(op-0x78)/2; }
    bool shift=operation>=5 && operation<=7;
    u8 type=is_long?T_LONG:T_INT;
    struct value b=pop(v,f,shift?T_INT:type),a=pop(v,f,type);
    if (!good(v)) return false;
    u64 x=a.bits,y=b.bits,z=0,mask=is_long?~0ull:0xffffffffull;
    u32 bits=is_long?64:32;
    switch(operation) {
    case 0: z=x+y; break;
    case 1: z=x-y; break;
    case 2: z=x*y; break;
    case 3: case 4: {
        if (!y) return fail(v,NV_JAVA_EXCEPTION,"ArithmeticException: division by zero");
        bool xn=(x>>(bits-1))!=0,yn=(y>>(bits-1))!=0;
        u64 ax=xn?(0ull-x)&mask:x, ay=yn?(0ull-y)&mask:y;
        z=operation==3?ax/ay:ax%ay;
        if (operation==3?xn!=yn:xn) z=0ull-z;
        break;
    }
    case 5: z=x<<(y&(bits-1)); break;
    case 6: {
        u32 n=y&(bits-1); z=x>>n;
        if (n && x>>(bits-1)) z|=mask<<(bits-n);
        break;
    }
    case 7: z=x>>(y&(bits-1)); break;
    case 8: z=x&y; break;
    case 9: z=x|y; break;
    case 10: z=x^y; break;
    default: return fail(v,NV_JAVA_UNSUPPORTED,"Arithmetic opcode is not implemented");
    }
    return push(v,f,(struct value){z&mask,type});
}
static bool array_access(struct vm *v,struct frame *f,u8 op) {
    bool store=op>=0x4f;
    u32 kind=op-(store?0x4f:0x2e);
    static const u8 types[]={T_INT,T_LONG,T_EMPTY,T_EMPTY,T_REF,T_INT,T_INT,T_INT};
    static const u8 classes[]={C_INTS,C_LONGS,255,255,C_OBJECTS,C_BYTES,C_CHARS,C_SHORTS};
    if (!types[kind]) return fail(v,NV_JAVA_UNSUPPORTED,"Floating-point array is not implemented");
    struct value value=store?pop(v,f,types[kind]):(struct value){0,types[kind]};
    struct value index=pop(v,f,T_INT),array=pop(v,f,T_REF);
    struct object *o=good(v)?object(v,array):NULL;
    if (!o) return false;
    if (o->kind!=O_ARRAY || (o->cls!=classes[kind] && !(kind==5 && o->cls==C_BOOLS) &&
        !(kind==4 && (app_array(o->cls) || o->cls==C_STRINGS)))) return fail(v,NV_JAVA_VERIFY,"Wrong array element type");
    if (index.bits>=o->length) return fail(v,NV_JAVA_EXCEPTION,"ArrayIndexOutOfBoundsException");
    if (store) {
        if (kind==4 && !array_store_compatible(v,o,value)) return false;
        if (kind==5) value.bits=o->cls==C_BOOLS?value.bits&1:value.bits&255;
        else if (kind>=6) value.bits&=65535;
        o->data[index.bits]=value.bits; return true;
    }
    value.bits=o->data[index.bits];
    if (kind==5 && o->cls==C_BYTES) value.bits=(u32)signed8(value.bits);
    if (kind==7) value.bits=(u32)signed16(value.bits);
    return push(v,f,value);
}
static bool execute(struct vm *v) {
    static const u8 types[]={T_INT,T_LONG,T_EMPTY,T_EMPTY,T_REF};
    while (v->depth && good(v)) {
        struct frame *f=&v->frames[v->depth-1]; v->c=v->classes[f->owner];
        struct method *m=&v->classes[f->owner]->methods[f->method];
        if (f->pc>=m->length || !m->starts[f->pc]) return fail(v,NV_JAVA_VERIFY,"Execution fell outside method");
        if (v->result->instructions==v->limit) return fail(v,NV_JAVA_LIMIT,"Java instruction budget exhausted");
        ++v->result->instructions;
        if (!(v->result->instructions&4095) && v->host.poll && v->host.poll(v->host.context))
            return fail(v,NV_JAVA_IO,"Java execution cancelled");
        u32 pc=f->pc; u8 op=m->code[f->pc++]; const u8 *code=m->code;
        v->result->pc=pc; v->result->method=f->method; v->result->class_index=f->owner; v->result->opcode=op;
        if (op==0) continue;
        if (op==1) { push(v,f,reference(0)); continue; }
        if (op>=2 && op<=8) { push(v,f,integer((u32)op-3)); continue; }
        if (op==9 || op==10) { push(v,f,wide(op-9)); continue; }
        if (op==0x10) { push(v,f,integer((u32)signed8(code[f->pc++]))); continue; }
        if (op==0x11) { push(v,f,integer((u32)signed16((u32)code[f->pc]<<8|code[f->pc+1]))); f->pc+=2; continue; }
        if (op>=0x12 && op<=0x14) {
            u32 index=op==0x12?code[f->pc++]:((u32)code[f->pc]<<8|code[f->pc+1]);
            if (op!=0x12) f->pc+=2;
            if (!index || index>=v->c->pool_count) return fail(v,NV_JAVA_FORMAT,"Invalid ldc index");
            struct cp *c=&v->c->pool[index];
            if (op==0x14 && c->tag==5) push(v,f,wide(c->bits));
            else if (op!=0x14 && c->tag==3) push(v,f,integer(c->bits));
            else if (op!=0x14 && c->tag==8) { u32 id=string_constant(v,index); if (id) push(v,f,reference(id)); }
            else return fail(v,NV_JAVA_UNSUPPORTED,"ldc constant kind is not implemented");
            continue;
        }
        if (op>=0x15 && op<=0x2d) {
            u32 kind=op<=0x19?op-0x15:(op-0x1a)/4, index=op<=0x19?code[f->pc++]:(op-0x1a)%4;
            if (!types[kind]) return fail(v,NV_JAVA_UNSUPPORTED,"Floating-point local is not implemented");
            local_load(v,f,index,types[kind]); continue;
        }
        if (op>=0x2e && op<=0x35) { array_access(v,f,op); continue; }
        if (op>=0x36 && op<=0x4e) {
            u32 kind=op<=0x3a?op-0x36:(op-0x3b)/4,index=op<=0x3a?code[f->pc++]:(op-0x3b)%4;
            if (!types[kind]) return fail(v,NV_JAVA_UNSUPPORTED,"Floating-point local is not implemented");
            struct value val=pop(v,f,types[kind]);
            if (good(v)) local_store(v,f,index,val);
            continue;
        }
        if (op>=0x4f && op<=0x56) { array_access(v,f,op); continue; }
        if (op>=0x57 && op<=0x5f) { stack_op(v,f,op); continue; }
        if (op>=0x60 && op<=0x83) { arithmetic(v,f,op); continue; }
        if (op==0x84) {
            u32 index=code[f->pc++]; i64 delta=signed8(code[f->pc++]);
            increment(v,f,index,delta);
            continue;
        }
        if (op==0x85 || op==0x88 || (op>=0x91 && op<=0x93)) {
            struct value a=pop(v,f,op==0x88?T_LONG:T_INT);
            if (!good(v)) return false;
            if (op==0x85) push(v,f,wide((u64)signed32(a.bits)));
            else if (op==0x91) push(v,f,integer((u32)signed8(a.bits&255)));
            else if (op==0x92) push(v,f,integer(a.bits&65535));
            else if (op==0x93) push(v,f,integer((u32)signed16(a.bits&65535)));
            else push(v,f,integer(a.bits));
            continue;
        }
        if (op==0x94) {
            struct value b=pop(v,f,T_LONG),a=pop(v,f,T_LONG);
            i64 x=signed64(a.bits),y=signed64(b.bits);
            if (good(v)) push(v,f,integer(x<y?0xffffffffu:x>y?1:0));
            continue;
        }
        if ((op>=0x99 && op<=0xa7) || op==0xc6 || op==0xc7 || op==0xc8) {
            i64 delta=op==0xc8?signed32(big32(code+f->pc)):signed16((u32)code[f->pc]<<8|code[f->pc+1]);
            f->pc+=op==0xc8?4:2; bool take=false;
            if (op==0xa7 || op==0xc8) take=true;
            else if (op==0xc6 || op==0xc7) { struct value a=pop(v,f,T_REF); take=op==0xc6?!a.bits:a.bits!=0; }
            else if (op==0xa5 || op==0xa6) { struct value b=pop(v,f,T_REF),a=pop(v,f,T_REF); take=op==0xa5?a.bits==b.bits:a.bits!=b.bits; }
            else {
                i64 b=op>=0x9f?signed32(pop(v,f,T_INT).bits):0,a=signed32(pop(v,f,T_INT).bits);
                u32 condition=op-(op>=0x9f?0x9f:0x99);
                take=condition==0?a==b:condition==1?a!=b:condition==2?a<b:condition==3?a>=b:condition==4?a>b:a<=b;
            }
            if (take && good(v) && branch_target(v,m,pc,delta)) f->pc=(u32)((i64)pc+delta);
            continue;
        }
        if (op==0xaa || op==0xab) {
            i64 key=signed32(pop(v,f,T_INT).bits); u32 p=(pc+4)&~3u;
            i64 delta=signed32(big32(code+p));
            if (op==0xaa) {
                i64 low=signed32(big32(code+p+4)),high=signed32(big32(code+p+8));
                if (key>=low && key<=high) delta=signed32(big32(code+p+12+(u32)(key-low)*4));
            } else {
                u32 n=big32(code+p+4);
                for (u32 i=0;i<n;++i) if (key==signed32(big32(code+p+8+i*8))) { delta=signed32(big32(code+p+12+i*8)); break; }
            }
            if (good(v) && branch_target(v,m,pc,delta)) f->pc=(u32)((i64)pc+delta);
            continue;
        }
        if (op>=0xac && op<=0xb1) {
            u8 type=op==0xb1?T_VOID:types[op-0xac]; struct signature sig;
            if (!type) return fail(v,NV_JAVA_UNSUPPORTED,"Floating-point return is not implemented");
            if (!signature(v,m->desc,&sig) || sig.result!=type) return fail(v,NV_JAVA_VERIFY,"Wrong return type");
            struct value val=type==T_VOID?empty():pop(v,f,type);
            if (!good(v)) return false;
            if (!reference_fits(v,val,m->desc,sig.return_at)) return false;
            if (type==T_INT) {
                u8 declared=v->c->pool[m->desc].text[sig.return_at];
                if (declared=='B') val.bits=(u32)signed8(val.bits&255);
                if (declared=='S') val.bits=(u32)signed16(val.bits&65535);
                if (declared=='C') val.bits&=65535;
                if (declared=='Z') val.bits&=1;
            }
            /* JVMS ireturn/return discard the rest of this frame's stack. */
            if (f->initializer) v->c->state=2;
            --v->depth;
            if (v->depth && type!=T_VOID) push(v,&v->frames[v->depth-1],val);
            continue;
        }
        if (op>=0xb2 && op<=0xb8) {
            u16 index=(u32)code[f->pc]<<8|code[f->pc+1]; f->pc+=2;
            if (op<=0xb5) field_access(v,f,op,index,pc); else invoke(v,f,op,index,pc);
            continue;
        }
        if (op==0xba) {
            u16 index=(u32)code[f->pc]<<8|code[f->pc+1];
            if (code[f->pc+2] || code[f->pc+3]) return fail(v,NV_JAVA_FORMAT,"Invalid invokedynamic padding");
            f->pc+=4; concat(v,f,index); continue;
        }
        if (op==0xbb) {
            u16 index=(u32)code[f->pc]<<8|code[f->pc+1]; f->pc+=2;
            struct java_class *owner=resolve_class(v,index);
            if (!owner) return false;
            if (initialize(v,owner,f,pc)) continue;
            u32 id=new_object(v,O_INSTANCE,C_APP+owner->id,owner->field_count,owner->field_count*8);
            if (id) push(v,f,reference(id));
            continue;
        }
        if (op==0xbc || op==0xbd) {
            int cls;
            if (op==0xbc) {
                u8 kind=code[f->pc++];
                cls=kind==4?C_BOOLS:kind==5?C_CHARS:kind==8?C_BYTES:kind==9?C_SHORTS:kind==10?C_INTS:kind==11?C_LONGS:-1;
                if (cls<0) return fail(v,NV_JAVA_UNSUPPORTED,"Primitive array type is not implemented");
            } else {
                int element=class_id(v,(u32)code[f->pc]<<8|code[f->pc+1]); f->pc+=2;
                cls=app_instance(element)?C_APP_ARRAY+element-C_APP:element==C_OBJECT?C_OBJECTS:element==C_STRING?C_STRINGS:-1;
                if (cls<0) return fail(v,NV_JAVA_UNSUPPORTED,"Reference array type is not implemented");
            }
            struct value length=pop(v,f,T_INT);
            if (!good(v)) return false;
            if (length.bits&0x80000000u) return fail(v,NV_JAVA_EXCEPTION,"NegativeArraySizeException");
            if (length.bits>v->capacity/8) return fail(v,NV_JAVA_MEMORY,"Java array exceeds arena");
            u32 id=new_object(v,O_ARRAY,cls,length.bits,(u32)length.bits*8);
            if (id) push(v,f,reference(id));
            continue;
        }
        if (op==0xbe) {
            struct object *o=object(v,pop(v,f,T_REF));
            if (!o || o->kind!=O_ARRAY) return fail(v,NV_JAVA_VERIFY,"arraylength requires an array");
            push(v,f,integer(o->length)); continue;
        }
        if (op==0xc0 || op==0xc1) {
            u16 index=(u32)code[f->pc]<<8|code[f->pc+1]; f->pc+=2;
            int cls=class_id(v,index); struct value val=pop(v,f,T_REF);
            if (!good(v)) return false;
            struct object *o=val.bits?object(v,val):NULL; bool matches=o && instance_of(o,cls);
            if (!good(v)) return false;
            if (op==0xc1) push(v,f,integer(matches));
            else if (!val.bits || matches) push(v,f,val);
            else return fail(v,NV_JAVA_EXCEPTION,"ClassCastException");
            continue;
        }
        if (op==0xc4) {
            u8 sub=code[f->pc++]; u32 index=(u32)code[f->pc]<<8|code[f->pc+1]; f->pc+=2;
            if (sub==0x84) {
                i64 delta=signed16((u32)code[f->pc]<<8|code[f->pc+1]); f->pc+=2;
                increment(v,f,index,delta);
            } else {
                bool store=sub>=0x36; u8 type=types[sub-(store?0x36:0x15)];
                if (!type) return fail(v,NV_JAVA_UNSUPPORTED,"Floating-point wide local is not implemented");
                if (store) { struct value val=pop(v,f,type); if (good(v)) local_store(v,f,index,val); }
                else local_load(v,f,index,type);
            }
            continue;
        }
        return fail(v,NV_JAVA_UNSUPPORTED,"Bytecode is not implemented");
    }
    return good(v);
}

int nv_java_run(const void *bytes,u32 length,void *arena,u32 arena_size,
                const char *const *args,u32 argc,u32 instruction_limit,
                const struct nv_java_host *host,struct nv_java_result *result) {
    if (!result) return NV_JAVA_FORMAT;
    *result=(struct nv_java_result){.message="Java program completed",.method=0xffff};
    if (!bytes || !arena || ((uptr)arena&15) || !host || !host->write ||
        (argc && !args) || argc>ARG_MAX || !instruction_limit || length>NV_JAVA_CLASS_MAX) {
        result->status=NV_JAVA_FORMAT; result->message="Invalid Java runtime arguments"; return result->status;
    }
    if (arena_size<sizeof(struct vm)+4096) {
        result->status=NV_JAVA_MEMORY; result->message="Java arena is too small"; return result->status;
    }
    struct vm *v=arena; memset(v,0,sizeof(*v));
    v->arena=arena; v->capacity=arena_size; v->used=(sizeof(*v)+15u)&~15u;
    v->result=result; v->limit=instruction_limit; v->host=*host;
    v->c=allocate(v,sizeof(*v->c));
    if (!v->c) goto done;
    v->classes[v->class_count++]=v->c;
    if (!parse_class(v,bytes,length)) goto done;
    result->major=v->c->major;
    if (host->main_class) {
        struct cp *name=class_name(v->c);
        if (strlen(host->main_class)!=name->length || memcmp(host->main_class,name->text,name->length)) {
            fail(v,NV_JAVA_VERIFY,"Entry class name does not match classpath request"); goto done;
        }
    }
    if (new_object(v,O_PRINT,C_PRINT,0,0)!=1 || !prepare_fields(v)) goto done;
    int main=method_named(v,"main","([Ljava/lang/String;)V");
    if (main<0 || (v->c->methods[main].flags&9)!=9) { fail(v,NV_JAVA_VERIFY,"public static void main(String[]) not found"); goto done; }
    if (initialize(v,v->c,NULL,0) && !execute(v)) goto done;
    v->c=v->classes[0];
    u32 array=new_object(v,O_ARRAY,C_STRINGS,argc,argc*8);
    if (!array) goto done;
    for (u32 i=0;i<argc && good(v);++i) {
        if (!args[i]) { fail(v,NV_JAVA_FORMAT,"Null application argument"); break; }
        v->objects[array]->data[i]=argument_string(v,args[i]);
    }
    if (!good(v)) goto done;
    struct value argument=reference(array);
    if (enter(v,main,&argument,1)) execute(v);
done:
    result->heap_bytes=v->used;
    return result->status;
}
