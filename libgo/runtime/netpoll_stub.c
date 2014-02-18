// Copyright 2013 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// +build plan9

#include "runtime.h"
#include "malloc.h"

// Polls for ready network connections.
// Returns list of goroutines that become runnable.
G*
runtime_netpoll(bool block)
{
	// Implementation for platforms that do not support
	// integrated network poller.
	USED(block);
	return nil;
}

void
runtime_netpoll_scan(void (*addroot)(Obj))
{
	USED(addroot);
}
