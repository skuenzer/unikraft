BEGIN {
	print "/* Automatically generated file; DO NOT EDIT */"
	print ""
	print "#ifndef __UK_STORE_H__"
	print "#error Do not include this header directly"
	print "#endif /* __UK_STORE_H__ */\n"

	entries = 0
}

/[a-zA-Z0-9]+/{
	printf "#define __UK_STORE_HAVE_%s 1\n", $1;
	printf "#define __UK_STORE_%s (%d)\n\n", $1, (entries++);
}

END {
	printf "#define __UK_STORE_COUNT (%d)\n", entries;
}
