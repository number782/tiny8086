#!/bin/sh

LOCAL_PATH=`dirname $0`
LOCAL_PATH=`cd $LOCAL_PATH && pwd`

if [ -e src/patched.successfully ]; then
	exit 0
else
	[ -e uqm-hd ] || git clone --depth=1 https://git.code.sf.net/p/urquanmastershd/git-new uqm-hd || exit 1
	ln -s uqm-hd/src src
	patch -p1 < android.diff || exit 1
	git -C src add config_unix.h
	touch src/patched.successfully
fi
