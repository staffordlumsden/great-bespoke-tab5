package main

import (
	"context"
	"errors"
	"io"
	"os"
	"path"
	"strings"
	"time"

	"golang.org/x/net/webdav"
)

const transferChunk = 2048

type driveFS struct{ drive *serialDrive }

func cleanName(name string) (string, error) {
	name = path.Clean("/" + name)
	for _, part := range strings.Split(strings.TrimPrefix(name, "/"), "/") {
		if strings.HasPrefix(part, ".") { return "", os.ErrPermission }
	}
	return name, nil
}

func (fsys driveFS) Mkdir(_ context.Context, name string, _ os.FileMode) error {
	name, err := cleanName(name); if err != nil { return err }
	_, err = fsys.drive.request(map[string]any{"op":"mkdir", "path":name}); return mapError(err)
}
func (fsys driveFS) RemoveAll(_ context.Context, name string) error {
	name, err := cleanName(name); if err != nil { return err }
	_, err = fsys.drive.request(map[string]any{"op":"remove", "path":name}); return mapError(err)
}
func (fsys driveFS) Rename(_ context.Context, oldName, newName string) error {
	oldName, err := cleanName(oldName); if err != nil { return err }
	newName, err = cleanName(newName); if err != nil { return err }
	_, err = fsys.drive.request(map[string]any{"op":"rename", "path":oldName, "to":newName}); return mapError(err)
}
func (fsys driveFS) Stat(_ context.Context, name string) (os.FileInfo, error) {
	name, err := cleanName(name); if err != nil { return nil, err }
	item, err := fsys.drive.stat(name); if err != nil { return nil, mapError(err) }
	if name == "/" { item.Name = "CYD SD Card" } else { item.Name = path.Base(name) }
	return fileInfo{entry:item}, nil
}
func (fsys driveFS) OpenFile(_ context.Context, name string, flag int, _ os.FileMode) (webdav.File, error) {
	name, err := cleanName(name); if err != nil { return nil, err }
	item, statErr := fsys.drive.stat(name)
	write := flag&(os.O_WRONLY|os.O_RDWR) != 0
	if statErr != nil && !write { return nil, mapError(statErr) }
	if statErr != nil && write && flag&os.O_CREATE == 0 { return nil, mapError(statErr) }
	if statErr == nil && write && flag&os.O_EXCL != 0 { return nil, os.ErrExist }
	if statErr == nil && item.Type == "dir" { return &remoteFile{fs:fsys, name:name, info:fileInfo{entry:item}}, nil }
	file := &remoteFile{fs:fsys, name:name, info:fileInfo{entry:entry{Name:path.Base(name),Type:"file"}}}
	if !write {
		file.info.entry.Size = item.Size
		return file, nil
	}
	temp, err := os.CreateTemp("", "cyd-usb-drive-*")
	if err != nil { return nil, err }
	file.temp, file.writable = temp, true
	if statErr == nil && flag&os.O_TRUNC == 0 {
		if err = file.copyRemoteToTemp(item.Size); err != nil { file.Close(); return nil, err }
	}
	if flag&os.O_APPEND != 0 { _, err = temp.Seek(0, io.SeekEnd) }
	return file, err
}

type fileInfo struct{ entry entry }
func (f fileInfo) Name() string       { return f.entry.Name }
func (f fileInfo) Size() int64        { return f.entry.Size }
func (f fileInfo) Mode() os.FileMode  { if f.IsDir() { return os.ModeDir|0755 }; return 0644 }
func (f fileInfo) ModTime() time.Time { return time.Unix(0,0) }
func (f fileInfo) IsDir() bool        { return f.entry.Type == "dir" }
func (f fileInfo) Sys() any           { return nil }

type remoteFile struct {
	fs driveFS
	name string
	info fileInfo
	position int64
	directory []os.FileInfo
	dirPosition int
	temp *os.File
	writable bool
	closed bool
}

func (f *remoteFile) Stat() (os.FileInfo,error) { return f.info, nil }
func (f *remoteFile) Read(p []byte) (int,error) {
	if f.closed || f.info.IsDir() { return 0, os.ErrInvalid }
	if f.temp != nil { return f.temp.Read(p) }
	if f.position >= f.info.Size() { return 0, io.EOF }
	if len(p) > transferChunk { p = p[:transferChunk] }
	data, err := f.fs.drive.read(f.name, f.position, len(p)); if err != nil { return 0, mapError(err) }
	count := copy(p,data); f.position += int64(count)
	if count == 0 { return 0, io.EOF }; return count,nil
}
func (f *remoteFile) Write(p []byte) (int,error) {
	if f.closed || !f.writable || f.temp == nil { return 0, os.ErrPermission }
	return f.temp.Write(p)
}
func (f *remoteFile) Seek(offset int64, whence int) (int64,error) {
	if f.temp != nil { return f.temp.Seek(offset,whence) }
	position := f.position
	switch whence { case io.SeekStart: position=offset; case io.SeekCurrent: position+=offset; case io.SeekEnd: position=f.info.Size()+offset; default: return 0,os.ErrInvalid }
	if position < 0 { return 0,os.ErrInvalid }; f.position=position; return position,nil
}
func (f *remoteFile) Readdir(count int) ([]os.FileInfo,error) {
	if !f.info.IsDir() { return nil,os.ErrInvalid }
	if f.directory == nil {
		items,err := f.fs.drive.list(f.name); if err != nil{return nil,mapError(err)}
		for _,item := range items { f.directory=append(f.directory,fileInfo{entry:item}) }
	}
	if f.dirPosition >= len(f.directory) { if count>0{return nil,io.EOF};return []os.FileInfo{},nil }
	end:=len(f.directory);if count>0&&f.dirPosition+count<end{end=f.dirPosition+count}
	result:=f.directory[f.dirPosition:end];f.dirPosition=end;return result,nil
}
func (f *remoteFile) copyRemoteToTemp(size int64) error {
	for offset:=int64(0);offset<size; {
		data,err:=f.fs.drive.read(f.name,offset,transferChunk);if err!=nil{return err}
		if len(data)==0{return io.ErrUnexpectedEOF};if _,err=f.temp.Write(data);err!=nil{return err};offset+=int64(len(data))
	}
	_,err:=f.temp.Seek(0,0);return err
}
func (f *remoteFile) Close() error {
	if f.closed{return nil};f.closed=true
	if f.temp==nil{return nil}
	name:=f.temp.Name();defer os.Remove(name)
	info,err:=f.temp.Stat();if err==nil&&f.writable{err=f.fs.drive.upload(f.name,f.temp,info.Size())}
	closeErr:=f.temp.Close();if err!=nil{return mapError(err)};return closeErr
}

func mapError(err error) error {
	if err==nil{return nil};text:=err.Error()
	if strings.Contains(text,"not_found"){return os.ErrNotExist}
	if strings.Contains(text,"exists"){return os.ErrExist}
	if strings.Contains(text,"forbidden"){return os.ErrPermission}
	return errors.New(text)
}
