// Copyright 2012 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package net

import "time"

// protocols contains minimal mappings between internet protocol
// names and numbers for platforms that don't have a complete list of
// protocol numbers.
//
// See http://www.iana.org/assignments/protocol-numbers
var protocols = map[string]int{
	"icmp": 1, "ICMP": 1,
	"igmp": 2, "IGMP": 2,
	"tcp": 6, "TCP": 6,
	"udp": 17, "UDP": 17,
	"ipv6-icmp": 58, "IPV6-ICMP": 58, "IPv6-ICMP": 58,
}

// LookupHost looks up the given host using the local resolver.
// It returns an array of that host's addresses.
func LookupHost(host string) (addrs []string, err error) {
	return lookupHost(host)
}

// LookupIP looks up host using the local resolver.
// It returns an array of that host's IPv4 and IPv6 addresses.
func LookupIP(host string) (addrs []IP, err error) {
	return lookupIPMerge(host)
}

var lookupGroup singleflight

// lookupIPMerge wraps lookupIP, but makes sure that for any given
// host, only one lookup is in-flight at a time. The returned memory
// is always owned by the caller.
func lookupIPMerge(host string) (addrs []IP, err error) {
	addrsi, err, shared := lookupGroup.Do(host, func() (interface{}, error) {
		return lookupIP(host)
	})
	if err != nil {
		return nil, err
	}
	addrs = addrsi.([]IP)
	if shared {
		clone := make([]IP, len(addrs))
		copy(clone, addrs)
		addrs = clone
	}
	return addrs, nil
}

func lookupIPDeadline(host string, deadline time.Time) (addrs []IP, err error) {
	if deadline.IsZero() {
		return lookupIPMerge(host)
	}

	// TODO(bradfitz): consider pushing the deadline down into the
	// name resolution functions. But that involves fixing it for
	// the native Go resolver, cgo, Windows, etc.
	//
	// In the meantime, just use a goroutine. Most users affected
	// by http://golang.org/issue/2631 are due to TCP connections
	// to unresponsive hosts, not DNS.
	timeout := deadline.Sub(time.Now())
	if timeout <= 0 {
		err = errTimeout
		return
	}
	t := time.NewTimer(timeout)
	defer t.Stop()
	type res struct {
		addrs []IP
		err   error
	}
	resc := make(chan res, 1)
	go func() {
		a, err := lookupIPMerge(host)
		resc <- res{a, err}
	}()
	select {
	case <-t.C:
		err = errTimeout
	case r := <-resc:
		addrs, err = r.addrs, r.err
	}
	return
}

// LookupPort looks up the port for the given network and service.
func LookupPort(network, service string) (port int, err error) {
	return lookupPort(network, service)
}

// LookupCNAME returns the canonical DNS host for the given name.
// Callers that do not care about the canonical name can call
// LookupHost or LookupIP directly; both take care of resolving
// the canonical name as part of the lookup.
func LookupCNAME(name string) (cname string, err error) {
	return lookupCNAME(name)
}

// LookupSRV tries to resolve an SRV query of the given service,
// protocol, and domain name.  The proto is "tcp" or "udp".
// The returned records are sorted by priority and randomized
// by weight within a priority.
//
// LookupSRV constructs the DNS name to look up following RFC 2782.
// That is, it looks up _service._proto.name.  To accommodate services
// publishing SRV records under non-standard names, if both service
// and proto are empty strings, LookupSRV looks up name directly.
func LookupSRV(service, proto, name string) (cname string, addrs []*SRV, err error) {
	return lookupSRV(service, proto, name)
}

// LookupMX returns the DNS MX records for the given domain name sorted by preference.
func LookupMX(name string) (mx []*MX, err error) {
	return lookupMX(name)
}

// LookupNS returns the DNS NS records for the given domain name.
func LookupNS(name string) (ns []*NS, err error) {
	return lookupNS(name)
}

// LookupTXT returns the DNS TXT records for the given domain name.
func LookupTXT(name string) (txt []string, err error) {
	return lookupTXT(name)
}

// LookupAddr performs a reverse lookup for the given address, returning a list
// of names mapping to that address.
func LookupAddr(addr string) (name []string, err error) {
	return lookupAddr(addr)
}
