#include <qemu-plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    uint64_t instructions, reads, writes;
} Site;

static Site *sites;
static uint64_t code_start, code_end, stack_start, stack_end;

static void execute(unsigned cpu, void *data) {
    (void)cpu;
    ((Site *)data)->instructions++;
}

static void memory(unsigned cpu, qemu_plugin_meminfo_t info, uint64_t address, void *data) {
    (void)cpu;
    if (address < stack_start || address >= stack_end) {
        return;
    }
    Site *site = data;
    if (qemu_plugin_mem_is_store(info)) {
        site->writes++;
    } else {
        site->reads++;
    }
}

static void translate(struct qemu_plugin_tb *tb, void *data) {
    (void)data;
    for (size_t i = 0; i < qemu_plugin_tb_n_insns(tb); i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t pc = qemu_plugin_insn_vaddr(insn);
        if (pc >= code_start && pc < code_end) {
            Site *site = &sites[(pc - code_start) / 2];
            qemu_plugin_register_vcpu_insn_exec_cb(insn, execute, QEMU_PLUGIN_CB_NO_REGS, site);
            qemu_plugin_register_vcpu_mem_cb(insn, memory, QEMU_PLUGIN_CB_NO_REGS,
                                            QEMU_PLUGIN_MEM_RW, site);
        }
    }
}

static void finish(void *data) {
    (void)data;
    for (uint64_t pc = code_start; pc < code_end; pc += 2) {
        Site site = sites[(pc - code_start) / 2];
        if (site.instructions || site.reads || site.writes) {
            fprintf(stderr, "VM_SITE %" PRIx64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
                    pc, site.instructions, site.reads, site.writes);
        }
    }
    fputs("VM_PROFILE_END\n", stderr);
    free(sites);
}

static int range(const char *arg, const char *prefix, uint64_t *start, uint64_t *end) {
    char trailing;
    size_t length = strlen(prefix);
    return !strncmp(arg, prefix, length) &&
           sscanf(arg + length, "%" SCNx64 ":%" SCNx64 "%c", start, end, &trailing) == 2 &&
           *start < *end && *end <= UINT64_C(0x100000000);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                                          int argc, char **argv) {
    if (!info->system_emulation || info->system.smp_vcpus != 1 || argc != 2 ||
        !range(argv[0], "code=", &code_start, &code_end) || ((code_start | code_end) & 1) ||
        code_end - code_start > 128 * 1024 ||
        !range(argv[1], "stack=", &stack_start, &stack_end)) {
        fputs("vm-profile: expected one CPU and valid code and stack ranges\n", stderr);
        return -1;
    }
    sites = calloc((code_end - code_start) / 2, sizeof(*sites));
    if (!sites) {
        perror("vm-profile");
        return -1;
    }
    qemu_plugin_register_vcpu_tb_trans_cb(id, translate, NULL);
    qemu_plugin_register_atexit_cb(id, finish, NULL);
    return 0;
}
