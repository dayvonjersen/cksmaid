#!/bin/bash
UPDATED=0
update_h() {
	if ! grep -o "$1" mods/modtape.h &> /dev/null; then
		((UPDATED++))
		sed -i "s:/\* MODS \*/:/\* MODS \*/\n$1:" mods/modtape.h
	fi
}
update_c() {
	grep -o "$1" mods/modtape.c &> /dev/null ||
		sed -i "s:/\* MODS \*/:/\* MODS \*/\n\t$1:" mods/modtape.c
}

# make sure h file exists
[ -f "mods/modtape.h" ] || cat > mods/modtape.h <<EOF
/* THIS FILE IS GENERATED BY modcodegen.sh, DO NOT TOUCH */
#ifndef MODTAPE_H
#define MODTAPE_H

void tape_loadmods();

/* MODS */

#endif
EOF

# make sure c file exists
[ -f "mods/modtape.c" ] || cat > mods/modtape.c <<EOF
/* THIS FILE IS GENERATED BY modcodegen.sh, DO NOT TOUCH */
#include "../modules.h"
#include "modtape.h"

void
tape_loadmods()
{
	/* MODS */	
}
EOF

# update files
for m in mods/**/*.c; do
	INIT=$(grep -o '.*_init.*' $m)
	update_h "void $INIT;" 
	update_c "$INIT;" 
done

(( $UPDATED > 0 )) && echo "mod tape loading: added $UPDATED mods"
exit 0