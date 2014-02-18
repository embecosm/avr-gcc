// Copyright 2010 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Export guts for testing.

package runtime

var Fadd64 = fadd64
var Fsub64 = fsub64
var Fmul64 = fmul64
var Fdiv64 = fdiv64
var F64to32 = f64to32
var F32to64 = f32to64
var Fcmp64 = fcmp64
var Fintto64 = fintto64
var F64toint = f64toint

func entersyscall()
func exitsyscall()
func golockedOSThread() bool

var Entersyscall = entersyscall
var Exitsyscall = exitsyscall
var LockedOSThread = golockedOSThread

type LFNode struct {
	Next    *LFNode
	Pushcnt uintptr
}

func lfstackpush(head *uint64, node *LFNode)
func lfstackpop2(head *uint64) *LFNode

var LFStackPush = lfstackpush
var LFStackPop = lfstackpop2

type ParFor struct {
	body    *byte
	done    uint32
	Nthr    uint32
	nthrmax uint32
	thrseq  uint32
	Cnt     uint32
	Ctx     *byte
	wait    bool
}

func parforalloc2(nthrmax uint32) *ParFor
func parforsetup2(desc *ParFor, nthr, n uint32, ctx *byte, wait bool, body func(*ParFor, uint32))
func parfordo(desc *ParFor)
func parforiters(desc *ParFor, tid uintptr) (uintptr, uintptr)

var NewParFor = parforalloc2
var ParForSetup = parforsetup2
var ParForDo = parfordo

func ParForIters(desc *ParFor, tid uint32) (uint32, uint32) {
	begin, end := parforiters(desc, uintptr(tid))
	return uint32(begin), uint32(end)
}

func testSchedLocalQueue()
func testSchedLocalQueueSteal()

var TestSchedLocalQueue1 = testSchedLocalQueue
var TestSchedLocalQueueSteal1 = testSchedLocalQueueSteal

// func haveGoodHash() bool
// func stringHash(s string, seed uintptr) uintptr
// func bytesHash(b []byte, seed uintptr) uintptr
// func int32Hash(i uint32, seed uintptr) uintptr
// func int64Hash(i uint64, seed uintptr) uintptr

// var HaveGoodHash = haveGoodHash
// var StringHash = stringHash
// var BytesHash = bytesHash
// var Int32Hash = int32Hash
// var Int64Hash = int64Hash

// func GogoBytes() int32

var hashLoad float64 // declared in hashmap.c
var HashLoad = &hashLoad
