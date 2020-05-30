﻿

#ifndef __minst_h__
#define __minst_h__

#ifdef __cplusplus
extern "C" {
#endif

enum minst_type {
    mtype_b = 1,
    mtype_bcond,
    mtype_mov_reg,
    mtype_cmp
};

typedef int(* minst_parse_callback)(void *emu, struct minst *minst);

struct minst_blk {
    char *funcname;
    void *emu;

    /* 生成IR时，会产生大量的临时变量，这个是临时变量计数器 */
    int         tvar_id;
    struct dynarray tvar;

    /* 当程序按顺序解析所有指令时，把所有指令放入此数组，记得此数组要
    严格按照地址顺序排列 */
    struct dynarray allinst;

    /**/
    struct dynarray allcfg;

    /* 只处理系统寄存器列表 */
    /* 某寄存器所有def指令集合，数据为指令id */
    struct bitset     defs[32];

    /* 某寄存器所有use指令集合，数据为指令id */
    struct bitset     uses[32];

    struct dynarray     const_insts;

    struct {
        /* 全局变量，判断是否需要进行活跃性分析 */
        unsigned need_liveness : 1;
    } flag;

    struct minst    *trace[512];
    int trace_top;

    struct {
        unsigned char   data[16 * KB];
        int             len;
    } text_sec;

    minst_parse_callback minst_do;

    struct minst*       epilogue;
};

struct minst_node {
    struct minst *minst;
    struct minst_node *next;
};


#define CSM_IN          1
#define CSM             2
#define CSM_OUT         3
struct minst_cfg {
    struct minst_blk *blk;

    struct {
        unsigned dead_code : 1;
    } flag;

    int     id;
    int     csm;
    struct minst    *start;
    struct minst    *end;
};

#define minst_is_b(m)       (m->type == mtype_b)
#define minst_is_bcond(m)   (m->type == mtype_bcond)
#define minst_is_b0(m)      (minst_is_b(m) || minst_is_bcond(m))

struct minst {
    unsigned char *addr;
    int len;

    int id;
    struct bitset use;
    struct bitset def;
    struct bitset in;
    struct bitset out;

    struct bitset rd_in;
    struct bitset rd_out;
    struct bitset kills;

    struct minst_node preds;
    struct minst_node succs;

    enum minst_type type;

    struct {
        unsigned b_need_fixed : 2;
        unsigned b_cond_passed : 1;
        unsigned b_cond : 4;
        unsigned dead_code : 1;
        unsigned be_del : 1;
        /* live 前置指令 */
        unsigned prologue : 1;
        /* live 后置指令 */
        unsigned epilogue : 1;
        /* 是否在It块中，是的话，需要调整他的前置节点，为什么呢
        比如, a -> b -> c

        上面是3条顺序执行的指令，但是假如 b.flag.in_it_block == 1，那么需要额外增加一个a->c的跳转。
        a - b -> c
          ----->
        否则会导致他的活跃计算不正确
        */
        unsigned in_it_block : 1;
        unsigned is_t : 1;

        unsigned is_const : 1;
        unsigned is_trace : 1;
    } flag;

    unsigned long host_addr;            // jump address, need be fixed in second pass

    struct minst_cfg *cfg;

    struct {
        short lm;
        short ln;
    } cmp;

    /* 调用哪个reg_node去解析内容 */
    void *reg_node;

    int ld;
    /* FIXME:这里做了一个错误的假设，即一条指令只能def一个寄存器。所以只存了一个值。
    但是实际上，sub 指令可以同时操作目的寄存器和 aspr 寄存器 */
    union {
        int ld_imm;
        struct arm_cpsr apsr;
    };
};


#define live_def_set(blk, reg)       do { \
        bitset_set(&minst->def, reg, 1); \
        if ((reg < SYS_REG_NUM) && (reg > -1)) \
            bitset_set(&((blk)->defs[reg]), minst->id, 1); \
    } while (0)

#define live_use_set(blk, reg)       do { \
        bitset_set(&minst->use, reg, 1); \
        if ((reg < SYS_REG_NUM) && (reg > -1)) \
            bitset_set(&((blk)->uses[reg]), minst->id, 1); \
    } while (0)

#define live_use_clear(blk, reg)    do { \
        bitset_set(&minst->use, reg, 0); \
        if ((reg < SYS_REG_NUM) && (reg > -1)) \
            bitset_set(&((blk)->uses[reg]), minst->id, 0); \
    } while (0)

#define liveness_set2(blk, _def, _use, _use1)    do { \
        live_def_set(blk, _def); \
        live_use_set(blk, _use); \
        live_use_set(blk, _use1); \
    } while (0)

#define liveness_set(blk, _def, _use)    do { \
        live_def_set(blk, _def); \
        live_use_set(blk, _use); \
    } while (0)


struct minst_blk*   minst_blk_new(char *funcname);
void                minst_blk_delete(struct minst_blk *mblk);

void                minst_blk_init(struct minst_blk *blk, char *funcname, minst_parse_callback callback, void *emu);
void                minst_blk_uninit(struct minst_blk *blk);

struct minst*       minst_new(struct minst_blk *blk, unsigned char *code, int len, void *reg_node);
struct minst*       minst_new_copy(struct minst_cfg *cfg, struct minst *src);
struct minst*       minst_new_t(struct minst_cfg *cfg, enum minst_type type, void *reg_node, unsigned char *code, int len);
struct minst*       minst_change(struct minst *m, enum minst_type type, void *reg_node, unsigned char *code, int len);
void                minst_delete(struct minst *inst);
void                minst_restore(struct minst *minst);
int                 minst_succs_count(struct minst *minst);
int                 minst_preds_count(struct minst *minst);

struct minst_cfg*   minst_cfg_new(struct minst_blk *blk, struct minst *start, struct minst *end);
void                minst_cfg_delete(struct minst_cfg *cfg);

int                 minst_get_def(struct minst *minst);
#define             minst_get_use(m)        bitset_next_bit_pos(&((m)->use), 0)
#define             minst_set_const(m, imm) do { \
        m->flag.is_const = 1; \
        m->ld_imm = imm; \
    } while (0)

#define minst_get_true_label(_m)            (_m)->succs.next->minst
#define minst_get_false_label(_m)           (_m)->succs.minst
#define minst_is_tconst(_m)                 ((_m)->flag.is_const || (_m)->flag.is_trace)
#define minst_set_trace(_m, imm)    do { \
        _m->flag.is_trace = 1; \
        _m->ld_imm = imm; \
    } while (0)

struct minst*       minst_blk_find(struct minst_blk *blk, unsigned long addr);

void                minst_succ_add(struct minst *minst, struct minst *succ);
void                minst_pred_add(struct minst *minst, struct minst *pred);
void                minst_succ_del(struct minst *minst, struct minst *succ);
void                minst_pred_del(struct minst *minst, struct minst *pred);
#define minst_add_edge(minst, succ)     do { \
        minst_succ_add(minst, succ); \
        minst_pred_add(succ, minst); \
    } while (0)
#define minst_del_edge(minst, succ)     do { \
        minst_succ_del(minst, succ); \
        minst_pred_del(succ, minst); \
    } while (0)

#define minst_succs_foreach(m, snode)  for (snode = &m->succs; snode; snode = snode->next)
#define minst_preds_foreach(m, pnode)  for (pnode = &m->preds; pnode; pnode = pnode->next)

void                minst_blk_gen_cfg(struct minst_blk *blk);
/* 当我们删除一个cfg以后，会造成很多的cfg都变成不可达，删除所有 */
void                minst_blk_del_unreachable(struct minst_blk *blk, struct minst_cfg *cfg);

/* 做活跃性分析的时候，需要加上liveness专用的prologue和epilogue
live prologue 把所有的寄存器设置为def
live epilogue 把所有的寄存器设置为use
不这样做他会把function proglogue和epilog识别为死代码
*/
void                minst_blk_live_prologue_add(struct minst_blk *blk);
void                minst_blk_live_epilogue_add(struct minst_blk *blk);

int                 minst_blk_is_on_start_unique_path(struct minst_blk *blk, struct minst *def, struct minst *use);

#define minst_cfg_is_dead(_cfg)          ((minst_preds_count(_cfg->start) == 0) || ((minst_preds_count(_cfg->start) == 1) && _cfg->start->preds.minst->cfg == _cfg))

/* */
int                 minst_blk_liveness_calc(struct minst_blk *blk);

/* 死代码删除 */
int                 minst_blk_dead_code_elim(struct minst_blk *blk);

/* 生成到达定值, generate reaching definitions */
int                 minst_blk_gen_reaching_definitions(struct minst_blk *blk);

int                 minst_blk_value_numbering(struct minst_blk *blk);

int                 minst_blk_copy_propagation(struct minst_blk *blk);

int                 minst_blk_regalloc(struct minst_blk *blk);

/* 指令乱序，为接下去的寄存器分配优化做准备

为什么做这个优化呢，假如有一下代码:

    mov r6, 1234
    cmp r5, r0
    bgt label_1

    ... 
    ...
    b xxxx

label_1:
    mov r6, r5

    我们分析以上代码会发现，r6是不在label_1的入口活跃集合中的，在一些特殊的上下文中
    r6和r5是有一定的合并可能性，，但是由于 mov r6, 1234的存在导致进行这些优化会有一
    些麻烦，所以我们改成如下形式

    cmp r5, r0
    bgt label_1

    mov r6, 1234
    ...
    ...
    b xxxx

label_1:

    这样不用进行寄存器分配，只是做一些简单的复写传播之类的优化就能干掉一些mov指令了
*/
int                 minst_blk_out_of_order(struct minst_blk *blk);

/* 是否是常量状态机 

1. 当前cfg的end指令是否为bcond指令
2. 影响apsr的寄存器必须有2个
3. 一个是const，一是overdefined
4. overdefined的值，都必须是
4.1 const
4.2 值保护使用
// MCIC P.464
5. 无法被const propagation 中的 constant conditions干掉(?)
*/
int                 minst_cfg_is_const_state_machine(struct minst_cfg *cfg, int *reg);

int                 minst_cfg_classify(struct minst_blk *blk);

int                 minst_cfg_inst_count(struct minst_cfg *cfg);


/*
获取影响当前cfg跳转指令的最初reg，超过一个即失败

@return     != 0    minst
            NULL    error
*/
struct minst*       minst_cfg_apsr_get_overdefine_reg(struct minst_cfg *cfg, int *reg);

/* 条件常数传播 */
int                 minst_conditional_const_propagation(struct minst_blk *blk);

struct minst*       minst_get_last_const_definition(struct minst_blk *blk, struct minst *minst, int regm);
/* 获取从minst指令往前的，对regm的定义
假如有多个定义，返回空 */
struct minst*       minst_get_last_def(struct minst_blk *blk, struct minst *minst, int regm);
/* 获取trace流里的定义 

@regm   要查找的寄存器
@index  查找寄存器的位置
@before 从哪个位置开始
*/
struct minst*       minst_trace_get_def(struct minst_blk *blk, int regm, int *index, int before);
struct minst_cfg*   minst_trace_find_prev_cfg(struct minst_blk *blk, int *index, int before);
/*
1. 查找上一个有未定义bcond指令
2. 假如没有找到，返回上一个const bcond指令的前一个指令，最前为start cfg

@index      当前cfg
*/
struct minst*       minst_trace_find_prev_undefined_bcond(struct minst_blk *blk, int *index, int before);
struct minst*       minst_trace_find_prev_bcond(struct minst_blk *blk, int *index, int before);
/* 获取当前cfg内，某条指令的前的对reg_def的定义 */
struct minst*       minst_cfg_get_last_def(struct minst_cfg *cfg, struct minst *minst, int reg_def);

/*

@return     1       passed
            0       un-passed
            -1      cant calc
*/
int                 minst_bcond_symbo_exec(struct minst_cfg *cfg, struct minst *def_val);

int  minst_blk_get_all_branch_reg_const_def(struct minst_blk *blk, 
    struct minst *cfg, int pass, int reg_use, struct dynarray *d, struct dynarray *id);

#ifdef __cplusplus
}
#endif

#endif