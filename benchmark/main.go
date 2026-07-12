package main

import (
	"benchmark/pb"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"math/rand"
	"net"
	"sync"
	"sync/atomic"
	"time"

	"google.golang.org/protobuf/proto"
)

func sendEnvelope(conn net.Conn, env *pb.Envelope) error {
	data, err := proto.Marshal(env)
	if err != nil {
		return err
	}
	var hdr [4]byte
	binary.BigEndian.PutUint32(hdr[:], uint32(len(data)))
	if _, err := conn.Write(hdr[:]); err != nil {
		return err
	}
	_, err = conn.Write(data)
	return err
}

func readServerMessage(conn net.Conn) (*pb.ServerMessage, error) {
	var hdr [4]byte
	if _, err := io.ReadFull(conn, hdr[:]); err != nil {
		return nil, err
	}
	size := binary.BigEndian.Uint32(hdr[:])
	if size > 1<<20 {
		return nil, fmt.Errorf("message too large: %d", size)
	}
	buf := make([]byte, size)
	if _, err := io.ReadFull(conn, buf); err != nil {
		return nil, err
	}
	msg := &pb.ServerMessage{}
	if err := proto.Unmarshal(buf, msg); err != nil {
		return nil, err
	}
	return msg, nil
}

func worker(addr string, n int, wg *sync.WaitGroup, success *int64, fail *int64, latencies []time.Duration, idx int) {
	defer wg.Done()

	conn, err := net.DialTimeout("tcp", addr, 3*time.Second)
	if err != nil {
		fmt.Printf("[worker %d] connect failed: %v\n", idx, err)
		atomic.AddInt64(fail, int64(n))
		return
	}
	defer conn.Close()

	for i := 0; i < n; i++ {
		uid := fmt.Sprintf("bench%d", rand.Intn(1000000))
		pwd := "bench123"

		env := &pb.Envelope{
			Payload: &pb.Envelope_LoginReq{
				LoginReq: &pb.LoginRequest{
					Uid:    uid,
					Passwd: pwd,
				},
			},
		}

		start := time.Now()
		if err := sendEnvelope(conn, env); err != nil {
			atomic.AddInt64(fail, 1)
			continue
		}

		_, err := readServerMessage(conn)
		elapsed := time.Since(start)

		if err != nil {
			atomic.AddInt64(fail, 1)
		} else {
			atomic.AddInt64(success, 1)
			latencies[idx*n+i] = elapsed
		}
	}
}

func main() {
	addr := flag.String("addr", "127.0.0.1:9000", "server address")
	concurrency := flag.Int("c", 50, "concurrent connections")
	total := flag.Int("n", 10000, "total login requests")
	flag.Parse()

	fmt.Printf("Login QPS Benchmark\n")
	fmt.Printf("Server:   %s\n", *addr)
	fmt.Printf("Concurrency: %d\n", *concurrency)
	fmt.Printf("Total:    %d\n", *total)
	fmt.Println("--------------------------------------------------")

	var success, fail int64
	latencies := make([]time.Duration, *total)

	reqPerWorker := *total / *concurrency
	remainder := *total % *concurrency

	var wg sync.WaitGroup

	start := time.Now()

	for i := 0; i < *concurrency; i++ {
		n := reqPerWorker
		if i < remainder {
			n++
		}
		wg.Add(1)
		go worker(*addr, n, &wg, &success, &fail, latencies, i)
	}

	wg.Wait()
	elapsed := time.Since(start)

	qps := float64(success) / elapsed.Seconds()

	fmt.Printf("\nResults:\n")
	fmt.Printf("  Time:      %v\n", elapsed.Round(time.Millisecond))
	fmt.Printf("  Success:   %d\n", success)
	fmt.Printf("  Failed:    %d\n", fail)
	fmt.Printf("  QPS:       %.1f\n", qps)

	// Calculate latency stats (only from successful ones)
	var totalLat time.Duration
	count := int64(0)
	for _, l := range latencies {
		if l > 0 {
			totalLat += l
			count++
		}
	}
	if count > 0 {
		avg := totalLat / time.Duration(count)
		fmt.Printf("  Avg Lat:   %v\n", avg.Round(time.Microsecond))

		// Sort for percentiles
		sorted := make([]time.Duration, 0, count)
		for _, l := range latencies {
			if l > 0 {
				sorted = append(sorted, l)
			}
		}
		// Simple insertion sort (fast for small-medium data)
		for i := 1; i < len(sorted); i++ {
			key := sorted[i]
			j := i - 1
			for j >= 0 && sorted[j] > key {
				sorted[j+1] = sorted[j]
				j--
			}
			sorted[j+1] = key
		}

		p50 := sorted[len(sorted)*50/100]
		p90 := sorted[len(sorted)*90/100]
		p95 := sorted[len(sorted)*95/100]
		p99 := sorted[len(sorted)*99/100]
		fmt.Printf("  P50 Lat:   %v\n", p50.Round(time.Microsecond))
		fmt.Printf("  P90 Lat:   %v\n", p90.Round(time.Microsecond))
		fmt.Printf("  P95 Lat:   %v\n", p95.Round(time.Microsecond))
		fmt.Printf("  P99 Lat:   %v\n", p99.Round(time.Microsecond))
	}

	fmt.Println("--------------------------------------------------")
	fmt.Println("Done.")
}
