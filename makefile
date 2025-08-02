OWDIR = $(WATCOM)
NAME = winsock
OUTDIR = RELEASE
WTLIB = w16/libcsock.lib

CC = wcc
CFLAGS = -ml -3 -zq -bt=windows_dll -bm -bd -ox
LINK=wlink

all: $(OUTDIR) $(OUTDIR)/$(NAME).dll

$(OUTDIR):
	@mkdir $(OUTDIR)

$(OUTDIR)/$(NAME).dll: $(OUTDIR)/$(NAME).obj a.lnk
	$(LINK) format windows dll name $@ op quiet, eliminate, map=$(dir $@)$(NAME).map, implib=$(dir $@)$(NAME).imp @a.lnk
	chmod -x $@

a.lnk:
	echo file $(OUTDIR)/$(NAME).obj >$@
	echo libpath $(OWDIR)/lib286/win >>$@
	echo libpath $(OWDIR)/lib286/os2 >>$@
	echo lib windows.lib >>$@
ifneq ($(wildcard $(WTLIB)),)
	echo lib $(WTLIB) >>$@
endif

$(OUTDIR)/$(NAME).obj: $(NAME).c makefile
	$(CC) $(CFLAGS) -fo=$@ $(NAME).c

clean:
	$(RM) -r $(OUTDIR)
	$(RM) a.lnk
