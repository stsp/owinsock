ifeq ($(WATCOM),)
$(error WATCOM variable not set)
endif
ifeq ($(INCLUDE),)
export INCLUDE := $(WATCOM)/h
endif
NAME = winsock
DLLNAME = WINSOCK.DLL
OUTDIR = RELEASE
WTLIB = libd2sock/W16/libd2sock.lib

CC = wcc
DUMMY = $(shell which $(CC) 2>/dev/null)
ifneq ($(.SHELLSTATUS),0)
$(warning appending $(WATCOM)/binl64 to PATH)
export PATH := $(PATH):$(WATCOM)/binl64
endif
CFLAGS = -ml -3 -bt=windows -bd -zc -zw -zu -I$(WATCOM)/h/win \
  -I$(WATCOM)/h -Ilibd2sock/include
LINK = wlink

all: $(OUTDIR) $(OUTDIR)/$(DLLNAME)

$(OUTDIR):
	@mkdir $(OUTDIR)

$(OUTDIR)/$(DLLNAME): $(OUTDIR)/$(NAME).obj a.lnk $(NAME).lbc $(WTLIB)
	$(LINK) name $@ op quiet, eliminate, map=$(dir $@)$(NAME).map, implib=$(dir $@)$(NAME).imp @a.lnk
	chmod -x $@

a.lnk:
	echo file $(OUTDIR)/$(NAME).obj >$@
	echo option manyautodata >>$@
	echo system windows dll initinstance memory >>$@
	echo libfile libentry.obj >>$@
	echo export=$(NAME).lbc >>$@
	echo lib $(WTLIB) >>$@

$(NAME).lbc: $(NAME).def
	awk -f def16lbc.awk -v OUTFILE=$@ $<

$(OUTDIR)/$(NAME).obj: $(NAME).c makefile
	$(CC) $(CFLAGS) -fo=$@ $(NAME).c

$(WTLIB): libd2sock
	$(MAKE) -C $<

libd2sock:
	git submodule update --remote

clean:
	$(MAKE) -C libd2sock clean
	$(RM) -r $(OUTDIR)
	$(RM) a.lnk $(NAME).lbc
