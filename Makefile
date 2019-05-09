PROG=	indent
SRCS=	indent.c io.c lexi.c parse.c pr_comment.c

CFLAGS=		-O2 -fstack-protector -D_FORTIFY_SOURCE=2 -pie -fPIE
LDFLAGS=	-static -Wl,-z,now -Wl,-z,relro

$(PROG): $(SRCS)
	gcc $(CFLAGS) $(LDFLAGS) $(SRCS) -o $(PROG).out
