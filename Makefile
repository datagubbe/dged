# BSD-compatible makefile

.include "common.mk"

.sinclude "$(UNAME_S).mk"

.include "targets.mk"

# in this case we need a separate depend target
depend: $(DEPS)
	@:

.if !(make(clean))
.  for o in ${DEPS}
.    sinclude "$o"
.  endfor
.endif
