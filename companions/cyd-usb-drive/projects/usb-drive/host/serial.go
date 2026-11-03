package main

import (
	"bufio"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"
	"time"

	serial "go.bug.st/serial"
)

type response struct {
	Seq      uint32  `json:"seq"`
	OK       bool    `json:"ok"`
	Code     string  `json:"code"`
	Message  string  `json:"message"`
	Protocol string  `json:"protocol"`
	Version  string  `json:"version"`
	Type     string  `json:"type"`
	Size     int64   `json:"size"`
	Total    float64 `json:"total"`
	Free     float64 `json:"free"`
	Data     string  `json:"data"`
	Count    int     `json:"count"`
	Token    uint32  `json:"token"`
	Written  uint32  `json:"written"`
	More     bool    `json:"more"`
	Entries  []entry `json:"entries"`
}

type entry struct {
	Name string `json:"name"`
	Type string `json:"type"`
	Size int64  `json:"size"`
}

type serialDrive struct {
	file       serial.Port
	mu         sync.Mutex
	seq        uint32
	responses  chan response
	disconnect chan struct{}
	once       sync.Once
}

func serialCandidates(explicit string) []string {
	if explicit != "" { return []string{explicit} }
	patterns := []string{"/dev/cu.usbserial-*", "/dev/cu.SLAB_USBtoUART*"}
	if runtime.GOOS == "linux" { patterns = []string{"/dev/ttyUSB*", "/dev/ttyACM*"} }
	var found []string
	for _, pattern := range patterns { matches, _ := filepath.Glob(pattern); found = append(found, matches...) }
	sort.Strings(found)
	return found
}

func openSerial(port string) (*serialDrive, error) {
	mode := &serial.Mode{BaudRate:115200, DataBits:8, Parity:serial.NoParity, StopBits:serial.OneStopBit}
	file, err := serial.Open(port, mode)
	if err != nil { return nil, err }
	if err = file.SetDTR(false); err != nil { file.Close(); return nil, fmt.Errorf("clear DTR: %w", err) }
	if err = file.SetRTS(false); err != nil { file.Close(); return nil, fmt.Errorf("clear RTS: %w", err) }
	_ = file.ResetInputBuffer()
	drive := &serialDrive{file: file, responses: make(chan response, 8), disconnect: make(chan struct{})}
	go drive.readLoop()
	return drive, nil
}

func (d *serialDrive) readLoop() {
	scanner := bufio.NewScanner(d.file)
	scanner.Buffer(make([]byte, 4096), 64*1024)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if os.Getenv("CYD_USB_DEBUG") != "" { log.Printf("serial rx %q", line) }
		if !strings.HasPrefix(line, "UD1 {") { continue }
		var event struct{ Event string `json:"event"` }
		if json.Unmarshal([]byte(line[4:]), &event) == nil && event.Event == "disconnect" {
			d.once.Do(func() { close(d.disconnect) })
			continue
		}
		var reply response
		if json.Unmarshal([]byte(line[4:]), &reply) == nil && reply.Seq != 0 { d.responses <- reply }
	}
	d.once.Do(func() { close(d.disconnect) })
}

func (d *serialDrive) close() { _ = d.file.Close(); d.once.Do(func() { close(d.disconnect) }) }

func (d *serialDrive) request(fields map[string]any) (response, error) {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.seq++
	if d.seq == 0 { d.seq++ }
	fields["seq"] = d.seq
	payload, err := json.Marshal(fields)
	if err != nil { return response{}, err }
	if os.Getenv("CYD_USB_DEBUG") != "" { log.Printf("serial tx %s", payload) }
	if _, err = fmt.Fprintf(d.file, "UD1 %s\n", payload); err != nil { return response{}, err }
	timer := time.NewTimer(15 * time.Second)
	defer timer.Stop()
	for {
		select {
		case reply := <-d.responses:
			if reply.Seq != d.seq { continue }
			if !reply.OK { return reply, fmt.Errorf("%s: %s", reply.Code, reply.Message) }
			return reply, nil
		case <-timer.C:
			return response{}, errors.New("CYD file request timed out")
		case <-d.disconnect:
			return response{}, errors.New("CYD disconnected")
		}
	}
}

func (d *serialDrive) ping() error {
	reply, err := d.request(map[string]any{"op":"ping"})
	if err != nil { return err }
	if reply.Protocol != "UD1" { return errors.New("unexpected device protocol") }
	return nil
}

func (d *serialDrive) stat(name string) (entry, error) {
	reply, err := d.request(map[string]any{"op":"stat", "path":name})
	return entry{Name: filepath.Base(name), Type: reply.Type, Size: reply.Size}, err
}

func (d *serialDrive) list(name string) ([]entry, error) {
	var all []entry
	for offset := 0; ; offset = len(all) {
		reply, err := d.request(map[string]any{"op":"list", "path":name, "offset":offset})
		if err != nil { return nil, err }
		all = append(all, reply.Entries...)
		if !reply.More { return all, nil }
	}
}

func (d *serialDrive) read(name string, offset int64, length int) ([]byte, error) {
	reply, err := d.request(map[string]any{"op":"read", "path":name, "offset":offset, "length":length})
	if err != nil { return nil, err }
	data, err := base64.StdEncoding.DecodeString(reply.Data)
	if err != nil { return nil, err }
	if len(data) != reply.Count { return nil, errors.New("short CYD read") }
	return data, nil
}

func (d *serialDrive) upload(name string, file *os.File, size int64) error {
	if size < 0 || size > int64(^uint32(0)) { return errors.New("file is too large for this CYD") }
	reply, err := d.request(map[string]any{"op":"write_begin", "path":name, "size":size})
	if err != nil { return err }
	token := reply.Token
	if _, err = file.Seek(0, 0); err != nil { return err }
	buffer := make([]byte, 1536)
	var offset int64
	for {
		count, readErr := file.Read(buffer)
		if count > 0 {
			_, err = d.request(map[string]any{"op":"write_data", "token":token, "offset":offset, "data":base64.StdEncoding.EncodeToString(buffer[:count])})
			if err != nil { _, _ = d.request(map[string]any{"op":"write_abort"}); return err }
			offset += int64(count)
		}
		if readErr != nil {
			if !errors.Is(readErr, io.EOF) { _, _ = d.request(map[string]any{"op":"write_abort"}); return readErr }
			break
		}
	}
	_, err = d.request(map[string]any{"op":"write_commit", "token":token})
	return err
}

func findDrive(explicit string) (*serialDrive, string, error) {
	var failures []string
	for _, port := range serialCandidates(explicit) {
		drive, err := openSerial(port)
		// Opening common CYD USB-UART bridges can pulse reset. Give the ESP32
		// bootloader and SD mount time to finish before sending the first frame.
		if err == nil { time.Sleep(2 * time.Second); err = drive.ping() }
		if err == nil { return drive, port, nil }
		if drive != nil { drive.close() }
		failures = append(failures, port+": "+err.Error())
	}
	if len(failures) == 0 { return nil, "", errors.New("no USB serial device found") }
	return nil, "", errors.New(strings.Join(failures, "; "))
}
