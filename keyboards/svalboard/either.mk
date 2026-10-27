# One image for both halves (svalboard/either, svalboard/trackball/pmw3389/either).
# The half learns its side at boot: thumb strap, then the stored side record,
# else unknown (side.h). Included by those targets' rules.mk only, so no other
# build changes.
OPT_DEFS += -DSVAL_EITHER_HAND
SRC += side.c

# A one-side build (side.h): make svalboard/either:sval SVAL_BUILD_SIDE=left
# (or right). The same image code with the side fixed, so copying the file onto
# a half sets that half's side; released as a plain UF2 for that half only.
SVAL_BUILD_SIDE ?=
ifeq ($(strip $(SVAL_BUILD_SIDE)),left)
  OPT_DEFS += -DSVAL_BUILD_SIDE=0
else ifeq ($(strip $(SVAL_BUILD_SIDE)),right)
  OPT_DEFS += -DSVAL_BUILD_SIDE=1
else ifneq ($(strip $(SVAL_BUILD_SIDE)),)
  $(error SVAL_BUILD_SIDE '$(SVAL_BUILD_SIDE)' must be left, right or empty)
endif
