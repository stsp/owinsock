OWDIR = $(WATCOM)
NAME = winsock
OUTDIR = RELEASE
WTLIB = w16/libcsock.lib

CC = wcc
CFLAGS = -ml -3 -zc -bt=windows -bm -bd -ox -d3
LINK=wlink

all: $(OUTDIR) $(OUTDIR)/$(NAME).dll

$(OUTDIR):
	@mkdir $(OUTDIR)

$(OUTDIR)/$(NAME).dll: $(OUTDIR)/$(NAME).obj a.lnk
	$(LINK) name $@ op quiet, eliminate, map=$(dir $@)$(NAME).map, implib=$(dir $@)$(NAME).imp @a.lnk
	chmod -x $@

a.lnk:
	echo debug all >$@
	echo file $(OUTDIR)/$(NAME).obj >>$@
	echo option oneautodata >>$@
	echo system windows dll initinstance memory >>$@
	echo libfile libentry.obj >>$@
ifneq ($(wildcard $(WTLIB)),)
	echo lib $(WTLIB) >>$@
endif

$(OUTDIR)/$(NAME).obj: $(NAME).c makefile
	$(CC) $(CFLAGS) -fo=$@ $(NAME).c

clean:
	$(RM) -r $(OUTDIR)
	$(RM) a.lnk
