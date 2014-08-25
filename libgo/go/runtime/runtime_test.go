// Copyright 2012 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package runtime_test

import (
	"io"
	// "io/ioutil"
	// "os"
	// "os/exec"
	. "runtime"
	"runtime/debug"
	// "strconv"
	// "strings"
	"testing"
	"unsafe"
)

var errf error

func errfn() error {
	return errf
}

func errfn1() error {
	return io.EOF
}

func BenchmarkIfaceCmp100(b *testing.B) {
	for i := 0; i < b.N; i++ {
		for j := 0; j < 100; j++ {
			if errfn() == io.EOF {
				b.Fatal("bad comparison")
			}
		}
	}
}

func BenchmarkIfaceCmpNil100(b *testing.B) {
	for i := 0; i < b.N; i++ {
		for j := 0; j < 100; j++ {
			if errfn1() == nil {
				b.Fatal("bad comparison")
			}
		}
	}
}

func BenchmarkDefer(b *testing.B) {
	for i := 0; i < b.N; i++ {
		defer1()
	}
}

func defer1() {
	defer func(x, y, z int) {
		if recover() != nil || x != 1 || y != 2 || z != 3 {
			panic("bad recover")
		}
	}(1, 2, 3)
	return
}

func BenchmarkDefer10(b *testing.B) {
	for i := 0; i < b.N/10; i++ {
		defer2()
	}
}

func defer2() {
	for i := 0; i < 10; i++ {
		defer func(x, y, z int) {
			if recover() != nil || x != 1 || y != 2 || z != 3 {
				panic("bad recover")
			}
		}(1, 2, 3)
	}
}

func BenchmarkDeferMany(b *testing.B) {
	for i := 0; i < b.N; i++ {
		defer func(x, y, z int) {
			if recover() != nil || x != 1 || y != 2 || z != 3 {
				panic("bad recover")
			}
		}(1, 2, 3)
	}
}

/* The go tool is not present in gccgo.

// The profiling signal handler needs to know whether it is executing runtime.gogo.
// The constant RuntimeGogoBytes in arch_*.h gives the size of the function;
// we don't have a way to obtain it from the linker (perhaps someday).
// Test that the constant matches the size determined by 'go tool nm -S'.
// The value reported will include the padding between runtime.gogo and the
// next function in memory. That's fine.
func TestRuntimeGogoBytes(t *testing.T) {
	if GOOS == "nacl" {
		t.Skip("skipping on nacl")
	}

	dir, err := ioutil.TempDir("", "go-build")
	if err != nil {
		t.Fatalf("failed to create temp directory: %v", err)
	}
	defer os.RemoveAll(dir)

	out, err := exec.Command("go", "build", "-o", dir+"/hello", "../../../test/helloworld.go").CombinedOutput()
	if err != nil {
		t.Fatalf("building hello world: %v\n%s", err, out)
	}

	out, err = exec.Command("go", "tool", "nm", "-size", dir+"/hello").CombinedOutput()
	if err != nil {
		t.Fatalf("go tool nm: %v\n%s", err, out)
	}

	for _, line := range strings.Split(string(out), "\n") {
		f := strings.Fields(line)
		if len(f) == 4 && f[3] == "runtime.gogo" {
			size, _ := strconv.Atoi(f[1])
			if GogoBytes() != int32(size) {
				t.Fatalf("RuntimeGogoBytes = %d, should be %d", GogoBytes(), size)
			}
			return
		}
	}

	t.Fatalf("go tool nm did not report size for runtime.gogo")
}
*/

// golang.org/issue/7063
func TestStopCPUProfilingWithProfilerOff(t *testing.T) {
	SetCPUProfileRate(0)
}

// Addresses to test for faulting behavior.
// This is less a test of SetPanicOnFault and more a check that
// the operating system and the runtime can process these faults
// correctly. That is, we're indirectly testing that without SetPanicOnFault
// these would manage to turn into ordinary crashes.
// Note that these are truncated on 32-bit systems, so the bottom 32 bits
// of the larger addresses must themselves be invalid addresses.
// We might get unlucky and the OS might have mapped one of these
// addresses, but probably not: they're all in the first page, very high
// adderesses that normally an OS would reserve for itself, or malformed
// addresses. Even so, we might have to remove one or two on different
// systems. We will see.

var faultAddrs = []uint64{
	// low addresses
	0,
	1,
	0xfff,
	// high (kernel) addresses
	// or else malformed.
	0xffffffffffffffff,
	0xfffffffffffff001,
	// no 0xffffffffffff0001; 0xffff0001 is mapped for 32-bit user space on OS X
	// no 0xfffffffffff00001; 0xfff00001 is mapped for 32-bit user space sometimes on Linux
	0xffffffffff000001,
	0xfffffffff0000001,
	0xffffffff00000001,
	0xfffffff000000001,
	0xffffff0000000001,
	0xfffff00000000001,
	0xffff000000000001,
	0xfff0000000000001,
	0xff00000000000001,
	0xf000000000000001,
	0x8000000000000001,
}

func TestSetPanicOnFault(t *testing.T) {
	// This currently results in a fault in the signal trampoline on
	// dragonfly/386 - see issue 7421.
	if GOOS == "dragonfly" && GOARCH == "386" {
		t.Skip("skipping test on dragonfly/386")
	}

	old := debug.SetPanicOnFault(true)
	defer debug.SetPanicOnFault(old)

	for _, addr := range faultAddrs {
		if Compiler == "gccgo" && GOARCH == "386" && (addr&0xff000000) != 0 {
			// On gccgo these addresses can be used for
			// the thread stack.
			continue
		}
		testSetPanicOnFault(t, uintptr(addr))
	}
}

func testSetPanicOnFault(t *testing.T, addr uintptr) {
	if GOOS == "nacl" {
		t.Skip("nacl doesn't seem to fault on high addresses")
	}

	defer func() {
		if err := recover(); err == nil {
			t.Fatalf("did not find error in recover")
		}
	}()

	var p *int
	p = (*int)(unsafe.Pointer(addr))
	println(*p)
	t.Fatalf("still here - should have faulted on address %#x", addr)
}
