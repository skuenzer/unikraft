# script expects that max_args is set (0-6)
BEGIN {
	print "/* Auto generated file. DO NOT EDIT */\n\n"

	print "#include <uk/syscall.h>"
	print "#include <uk/print.h>\n"

	printf "long uk_syscall%d_r(long nr", max_args
	for (i = 1; i <= max_args; i++)
		printf ", long arg%d __maybe_unused", i
	printf ")\n{\n"
	print "\tlong ret;\n"

	print "\t__UK_SYSCALL_RETADDR_ENTRY();"
	print "\tswitch (nr) {"
}


/[a-zA-Z0-9]+-[0-9]+/{
	name = $1
	sys_name = "SYS_" name
	uk_syscall_r = "uk_syscall_r_" name
	args_nr = $2 + 0
	printf "#ifdef HAVE_uk_syscall_%s\n", name;
	printf "\tcase %s:\n", sys_name;
	printf "\t\tret = %s(", uk_syscall_r;
	for (i = 1; i < args_nr; i++) {
		if (i <= max_args)
			printf("arg%d, ", i)
		else
			printf("0x0, ")
	}
	if (args_nr > 0) {
		if (args_nr <= max_args)
			printf("arg%d", args_nr)
		else
			printf("0x0")
	}

	printf(");\n")
	printf "\t\tbreak;\n"
	printf "#endif /* HAVE_uk_syscall_%s */\n\n", name;
}

END {
	printf "\tdefault:\n"
	printf "\t\tuk_pr_debug(\"syscall \\\"%%s\\\" is not available\\n\", uk_syscall_name(nr));\n"
	printf "\t\tret = -ENOSYS;\n"
	printf "\t}\n"
	printf "\t__UK_SYSCALL_RETADDR_CLEAR();\n"
	printf "\treturn ret;\n"
	printf "}\n"
	printf "\n"
}
