OWDIR = $(WATCOM)
NAME = winsock
DLLNAME = WINSOCK.DLL
OUTDIR = RELEASE
WTLIB = w16/libcsock.lib

CC = wcc
CFLAGS = -ml -3 -bt=windows -bd -zc -zw -zu
LINK=wlink

all: $(OUTDIR) $(OUTDIR)/$(DLLNAME)

$(OUTDIR):
	@mkdir $(OUTDIR)

$(OUTDIR)/$(DLLNAME): $(OUTDIR)/$(NAME).obj a.lnk $(NAME).lbc
	$(LINK) name $@ op quiet, eliminate, map=$(dir $@)$(NAME).map, implib=$(dir $@)$(NAME).imp @a.lnk
	chmod -x $@

a.lnk:
	echo debug all >$@
	echo file $(OUTDIR)/$(NAME).obj >>$@
	echo option oneautodata >>$@
	echo option heapsize=32K >>$@
	echo option stack=8K >>$@
	echo system windows dll initinstance memory >>$@
	echo libfile libentry.obj >>$@
	echo export=$(NAME).lbc >>$@
ifneq ($(wildcard $(WTLIB)),)
	echo lib $(WTLIB) >>$@
endif

$(NAME).lbc: $(NAME).def
	awk -f def16lbc.awk -v OUTFILE=$@ $<

$(OUTDIR)/$(NAME).obj: $(NAME).c makefile
	$(CC) $(CFLAGS) -fo=$@ $(NAME).c

clean:
	$(RM) -r $(OUTDIR)
	$(RM) a.lnk $(NAME).lbc
