# BSD-compatible makefile

.include "common.mk"

.include "$(UNAME_S).mk"

# in this case we need a separate depend target
depend: $(DEPS)
	@:

.if !(make(clean))
.  for o in ${DEPS}
.    sinclude "$o"
.  endfor
.endif
