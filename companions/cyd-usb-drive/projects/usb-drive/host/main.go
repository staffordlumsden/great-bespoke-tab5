package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strings"
	"syscall"
	"time"

	"golang.org/x/net/webdav"
)

func mounted(path string) bool {
	output, _ := exec.Command("mount").Output()
	return strings.Contains(string(output), " on "+path+" ")
}

func unmount(path string) {
	if !mounted(path) { return }
	command := exec.Command("diskutil", "unmount", path)
	if runtime.GOOS == "linux" { command = exec.Command("fusermount", "-u", path) }
	_ = command.Run()
}

func main() {
	serial := flag.String("serial", "", "USB serial device (auto-detected by default)")
	listen := flag.String("listen", "127.0.0.1:17688", "local WebDAV listen address")
	serveOnly := flag.Bool("serve-only", false, "serve WebDAV without mounting it")
	flag.Parse()

	drive, port, err := findDrive(*serial)
	if err != nil { log.Fatal("CYD USB Drive: ", err) }
	defer drive.close()
	log.Printf("Connected to CYD USB Drive on %s", port)
	heartbeatStop := make(chan struct{})
	heartbeatFailed := make(chan struct{}, 1)
	go func() {
		ticker := time.NewTicker(2 * time.Second)
		defer ticker.Stop()
		for {
			select {
			case <-heartbeatStop: return
			case <-ticker.C:
				if err := drive.ping(); err != nil {
					select { case heartbeatFailed <- struct{}{}: default: }
					return
				}
			}
		}
	}()

	handler := &webdav.Handler{Prefix:"/", FileSystem:driveFS{drive:drive}, LockSystem:webdav.NewMemLS()}
	server := &http.Server{Addr:*listen, Handler:handler, ReadHeaderTimeout:15*time.Second}
	go func(){ if err:=server.ListenAndServe();err!=nil&&err!=http.ErrServerClosed{log.Fatal(err)} }()

	ejected := make(chan struct{})
	if *serveOnly || runtime.GOOS != "darwin" {
		fmt.Printf("CYD SD Card is available at http://%s/\n", *listen)
		if runtime.GOOS == "linux" {
			if _, lookupErr := exec.LookPath("gio"); lookupErr == nil {
				davURL := "dav://" + *listen + "/"
				_ = exec.Command("gio", "mount", davURL).Run()
				_ = exec.Command("gio", "open", davURL).Start()
			}
		}
	} else {
		home, _ := os.UserHomeDir()
		mountpoint := filepath.Join(home,"Volumes","CYD SD Card")
		if err=os.MkdirAll(mountpoint,0755);err!=nil{log.Fatal(err)}
		unmount(mountpoint)
		url := "http://"+*listen+"/"
		if output,mountErr:=exec.Command("/sbin/mount_webdav","-S","-v","CYD SD Card",url,mountpoint).CombinedOutput();mountErr!=nil{log.Fatalf("Finder mount failed: %v: %s",mountErr,strings.TrimSpace(string(output)))}
		defer unmount(mountpoint)
		_ = exec.Command("open",mountpoint).Run()
		log.Printf("Mounted %s in Finder", mountpoint)
		go func() {
			for {
				time.Sleep(time.Second)
				if !mounted(mountpoint) { close(ejected); return }
			}
		}()
	}

	stop := make(chan os.Signal,1);signal.Notify(stop,os.Interrupt,syscall.SIGTERM)
	select {
	case <-stop:
	case <-drive.disconnect: log.Print("CYD requested safe disconnect")
	case <-ejected: log.Print("Finder volume ejected")
	case <-heartbeatFailed: log.Print("CYD heartbeat stopped")
	}
	close(heartbeatStop)
	ctx,cancel:=context.WithTimeout(context.Background(),3*time.Second);defer cancel();_ = server.Shutdown(ctx)
}
