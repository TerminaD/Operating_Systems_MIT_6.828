// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display all stack frames", mon_backtrace },
	{ "step", "Single step when already in debugging console", mon_step },
	{ "exitstep", "Exit single stepping", mon_exitstep }
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	struct Eipdebuginfo info;

	uint32_t ebp = read_ebp();

    cprintf("Stack backtrace:\n");

    while (ebp) {
		if (debuginfo_eip(*(uint32_t *)(ebp + 4), &info) < 0)
			panic("Address not found in mon_backtrace");
        cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n         %s:%d: %.*s+%d\n", 
                ebp,
				*(uint32_t *)(ebp + 4),
				*(uint32_t *)(ebp + 8),
				*(uint32_t *)(ebp + 12),
                *(uint32_t *)(ebp + 16),
				*(uint32_t *)(ebp + 20),
				*(uint32_t *)(ebp + 24),
				info.eip_file,
				info.eip_line,
				info.eip_fn_namelen,
				info.eip_fn_name,
				*(uint32_t *)(ebp + 4) - info.eip_fn_addr);
        ebp = *(uint32_t *)ebp;
    }   
    return 0;
}

int
mon_step(int argc, char **argv, struct Trapframe *tf) {
	// print_trapframe(tf);

	if ((tf->tf_trapno != T_DEBUG) && (tf->tf_trapno != T_BRKPT)) {
		panic("Not already in debugging mode when calling step\n");
		return -1;
	}

	// Enable single step flag and disable resume flag
	uint32_t eflags = tf->tf_eflags;
	eflags |= 0x100;
	eflags &= ~0x10000;
	tf->tf_eflags = eflags;

	return -2; // To resume to user program
}

int
mon_exitstep(int argc, char **argv, struct Trapframe *tf) {
	if ((tf->tf_trapno != T_DEBUG) && (tf->tf_trapno == T_BRKPT)) {
		panic("Not already in debugging mode when calling exitstep\n");
		return -1;
	}
	if ((tf->tf_eflags & 0x100) == 0) {
		panic("Not in single stepping mode when calling exitstep\n");
		return -1;
	}
	
	// Enable resume flag and disable single step flag
	// TRY EDITING TF EFLAGS?
	uint32_t eflags = tf->tf_eflags;
	eflags |= 0x10000;
	eflags &= ~0x100;
	tf->tf_eflags = eflags;

	return -2;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
